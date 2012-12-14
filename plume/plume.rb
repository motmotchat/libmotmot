#
# plume.rb - Plume!  Plume, plume, plume!
#

require 'rubygems'

require 'dnsruby'
require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'conn/conn.rb'

DNSRuby = Dnsruby

class PlumeServer < PlumeConn

  LEGAL_OPS = %w(route)

  def initialize(key_file, crt_file, conn_table)
    super(key_file, crt_file)
    @conns = conn_table
  end

  #
  # Add a connection to our table.
  #
  def ssl_handshake_completed
    peer_cert = OpenSSL::X509::Certificate.new get_peer_cert
    name = OpenSSL::X509::Name.new(peer_cert.subject)
    @peer_handle = name.to_a.find { |a| a.first == 'CN' }[1]

    @conns[@peer_handle] = self
  end

  #
  # Remove a dropped connection from our table.
  #
  def unbind
    @conns.delete @peer_handle
  end

  private

  #
  # Route a message to a peer, via another Plume server or direct connection.
  #
  def route(cert, peer, op, payload)
    # Validate the peer name.
    email = parse_email(peer)
    return close_connection if email.nil?

    # If we have a connection to the peer, route directly.
    if @conns[peer]
      return @conns[peer].send_data [op, [cert, peer, payload]].to_msgpack
    end

    addr, port = '', 0

    # Determine the address and port of the peer's Plume server.
    DNSRuby::DNS.open do |dns|
      srv = dns.getresource("_plume._tcp.#{email.domain}", DNSRuby::Types.SRV)
      addr, port = srv.target, srv.port
    end

    return close_connection if addr == '.' or addr == '' or port == 0

    # Route the connection request to the peer's Plume server.
    EM.connect(addr, port, PlumeServer, key_file, crt_file, @conns) do |conn|
      conn.send_data ['route', [cert, peer, op, payload]].to_msgpack
    end
  end
end

conns = {}

key_file = 'pem/plume.key'
crt_file = 'pem/plume.crt'

port = ARGV[0] || '9000'

EM.run {
  EM.start_server 'localhost', port, PlumeServer, key_file, crt_file, conns
}
