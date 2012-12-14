#
# client_plume.rb - Connection between a client and a Plume server.
#

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'conn.rb'
require_relative '../rsa.rb'
require_relative '../util.rb'

class PlumeID
  attr_reader   :cert
  attr_accessor :ip, :port

  def initialize(cert)
    @cert = cert
  end
end

class UDPConn < EM::Connection
  attr_accessor :ip, :port
end

class ClientPlumeConn < PlumeConn

  LEGAL_OPS = %w(connect identify cert ack_udp udp)
  OP_PREFIX = 'recv_'

  def initialize(key_file, crt_file)
    super key_file, crt_file
    @peers = {}
    @udps = {}

    @prng = Random.new
    @timers = {}
  end

  def unbind
    puts "ClientPlumeConn: An error occurred." if error?
  end

  #
  # Send our connection info to a peer.
  #
  def connect(peer)
    if parse_email(peer).nil?
      puts "ClientPlumeConn: Invalid peer specified."
      return
    end

    # If we don't have the peer's certificate, request it.
    if not @peers[peer]
      send_data ['route', [cert.to_pem, peer, 'identify']].to_msgpack
      return
    end

    # If we don't know our UDP IP and port for this peer, ask the server.
    if not @udps[peer]
      cookie = @prng.rand(1 << 64)
      @udps[cookie] = peer

      send_data ['udp', [cert.to_pem, cookie]].to_msgpack
      return
    end

    # Sign our IP and port, encrypt the signature, and send it to our peer.
    ip, port = @udps[peer].ip, @udps[peer].port
    sig = OpenSSL::PKCS7.sign(cert, key, "#{ip}:#{port}")
    id_enc = @peers[peer].cert.public_key.public_encrypt_block(sig.to_pem)

    send_data ['route', [cert.to_pem, peer, 'connect', id_enc]].to_msgpack
  end

  private

  #
  # Receive a connection request from a peer.
  #
  def recv_connect(peer_cert, peer_id_enc)
    peer_cert = OpenSSL::X509::Certificate.new peer_cert
    p7 = OpenSSL::PKCS7.new(key.private_decrypt_block(peer_id_enc))

    ca_store = OpenSSL::X509::Store.new
    verify = p7.verify([peer_cert], ca_store, nil, OpenSSL::PKCS7::NOVERIFY)
    return close_connection unless verify

    puts p7.data
  end

  #
  # Receive a request for our cert.
  #
  def recv_identify(peer_cert, _)
    peer = cert_cn(peer_cert)
    send_data ['route', [cert.to_pem, peer, 'cert']].to_msgpack
  end

  #
  # Receive a peer's cert and try to connect again.
  #
  def recv_cert(peer_cert, _)
    peer_cert = OpenSSL::X509::Certificate.new peer_cert
    peer = cert_cn(peer_cert)

    @peers[peer] = PlumeID.new(peer_cert)
    connect peer
  end

  #
  # Receive the server's acknowledgment of our UDP self-identification request
  # and begin sending datagrams.
  #
  def recv_ack_udp(cookie)
    return unless @udps[cookie]

    @udps[@udps[cookie]] = EM::open_datagram_socket '0.0.0.0', 0, UDPConn

    @timers[cookie] = EM::PeriodicTimer.new(0.5) do
      domain = parse_email(cert_cn(cert)).domain
      addr, port = dns_get_srv("_plume-udp._udp.#{domain}")

      @udps[@udps[cookie]].send_datagram cookie.to_msgpack, addr, port
    end
  end

  #
  # Receive a response to a UDP self-identification request.
  #
  def recv_udp(cookie, ip_port)
    # Match the UDP connection with our IP and port.
    udp_conn = @udps[@udps[cookie]]
    udp_conn.ip, udp_conn.port = ip_port

    # Clear relevant state.
    @timers[cookie].cancel
    @timers.delete cookie
    @udps.delete cookie
  end
end
