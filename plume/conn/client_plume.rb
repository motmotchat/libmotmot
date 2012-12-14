#
# client_plume.rb - Connection between a client and a Plume server.
#

require 'msgpack'
require 'openssl'

require_relative 'conn.rb'
require_relative '../util.rb'

class PlumeID
  attr_reader   :cert
  attr_accessor :ip, :port

  def initialize(cert)
    @cert = cert
  end
end

class ClientPlumeConn < PlumeConn

  LEGAL_OPS = %w(connect identify cert)
  OP_PREFIX = 'recv_'

  def initialize(key_file, crt_file)
    super key_file, crt_file
    @peers = {}
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

    sig = OpenSSL::PKCS7.sign(cert, key, get_sockaddr.join(':'))
    id_enc = @peers[peer].cert.public_key.public_encrypt(sig.to_pem)

    send_data ['route', [cert.to_pem, peer, 'connect', id_enc]].to_msgpack
  end

  private

  #
  # Receive a connection request from a peer.
  #
  def recv_connect(peer_cert, peer_id_enc)
    p7 = OpenSSL::PKCS7.new(key.private_decrypt(peer_id_enc).to_pem)

    ca_store = OpenSSL::X509::Store.new
    verify = p7.verify([peer_cert], ca_store, nil, OpenSSL::PKCS7::NOVERIFY)
    return close_connection unless verify

    puts p7.data
  end

  #
  # Receive a request for our cert.
  #
  def recv_identify(peer_cert, _)
    peer = cert_cn(OpenSSL::X509::Certificate.new peer_cert)
    send_data ['route', [cert.to_pem, peer, 'cert']].to_msgpack
  end

  #
  # Receive a peer's cert and try to connect again.
  #
  def recv_cert(peer_cert, _)
    id = PlumeID.new (OpenSSL::X509::Certificate.new peer_cert)
    @peers[cert_cn(id.cert)] = id

    connect peer
  end
end
