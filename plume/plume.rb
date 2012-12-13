#
# plume.rb - Plume!  Plume, plume, plume!
#

require 'rubygems'

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'conn.rb'

$conns = {}

class Plume < PlumeConn

  KEY_FILE = 'pem/plume.key'
  CRT_FILE = 'pem/plume.crt'
  LEGAL_OPS = %w(route forward)

  #
  # Add a connection to our table.
  #
  def ssl_handshake_completed
    peer_cert = OpenSSL::X509::Certificate.new get_peer_cert
    name = OpenSSL::X509::Name.new(peer_cert.subject)
    @peer_handle = name.to_a.find { |a| a.first == 'CN' }[1]

    $conns[@peer_handle] = self
  end

  #
  # Remove a dropped connection from our table.
  #
  def unbind
    $conns.delete @peer_handle
  end

  private

  #
  # Route a message for our client to a peer by passing it to another Plume
  # server.
  #
  def route(cert, peer, op, payload)
    email = parse_email(peer)
    return close_connection if email.nil?

    # Determine the address and port of the peer's Plume server.
    # TODO: This.
    addr = 'localhost'
    port = '9002'

    # Route the connection request to the peer's Plume server.
    EM.connect(addr, port, Plume) do |plume|
      plume.send_data ['forward', [cert, peer, op, payload]].to_msgpack
    end
  end

  #
  # Pass a message to a peer.
  #
  def forward(cert, peer, op, payload)
    return close_connection if not $conns[peer]

    $conns[peer].send_data [op, [cert, peer, payload]].to_msgpack
  end
end

port = ARGV[0] || '9000'

EM.run { EM.start_server 'localhost', port, Plume }
