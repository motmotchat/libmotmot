#
# plume.rb - Plume!  Plume, plume, plume!
#

require 'rubygems'
require 'bundler/setup'

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'conn/conn.rb'
require_relative 'util.rb'

class PlumeServer < PlumeConn

  LEGAL_OPS = %w(route)

  def initialize(key_file, crt_file, conn_table)
    super key_file, crt_file
    @conns = conn_table
  end

  #
  # Add a connection to our table.
  #
  def ssl_handshake_completed
    @peer_handle = cert_cn(get_peer_cert)
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
  def route(cert, peer, op, payload=nil)
    # Validate the peer name.
    email = parse_email(peer)
    return close_connection if email.nil?

    # If we have a connection to the peer, route directly.
    if @conns[peer]
      return @conns[peer].send_data [op, [cert, payload]].to_msgpack
    end

    addr, port = '', 0

    # Determine the address and port of the peer's Plume server.
    addr, port = dns_get_srv("_plume._tcp.#{email.domain}")
    return close_connection if addr.nil? or port.nil?

    # Route the connection request to the peer's Plume server.
    EM.connect(addr, port, PlumeServer, key_file, crt_file, @conns) do |conn|
      conn.send_data ['route', [cert, peer, op, payload]].to_msgpack
    end
  end
end

conns = {}

plume_dir = File.expand_path "~/.plume/#{ENV['PLUME_HOSTNAME'].to_s}"
key_file = ENV['PLUME_KEY'] || plume_dir + '/plume.key'
crt_file = ENV['PLUME_CRT'] || plume_dir + '/plume.crt'

port = ARGV[0] || '42000'

EM.run {
  EM.start_server '0.0.0.0', port, PlumeServer, key_file, crt_file, conns
}
