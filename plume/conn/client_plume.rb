#
# client_plume.rb - Connection between a client and a Plume server.
#

require 'msgpack'
require 'openssl'

require_relative 'conn.rb'
require_relative '../util.rb'

class ClientPlumeConn < PlumeConn

  LEGAL_OPS = %w(connect)
  OP_PREFIX = 'recv_'

  def initialize(key_file, crt_file)
    super key_file, crt_file
    @peers = {}
  end

  def unbind
    puts "ClientPlumeConn: An error occurred." if error?
  end

  #
  # Request information from the Plume server to connect to a peer.
  #
  def connect(peer)
    if parse_email(peer).nil?
      puts "ClientPlumeConn: Invalid peer specified."
      return
    end

    @peers[peer] = true

    ip, port = get_sockaddr
    cc = OpenSSL::PKCS7.sign(cert, key, [cert.to_pem, ip, port].to_msgpack)

    send_data ['route', [cert.to_pem, peer, 'connect', cc.to_pem]].to_msgpack
  end

  private

  #
  # Receive a connection request from a peer.
  #
  def recv_connect(peer_cert, _, payload)
  end
end
