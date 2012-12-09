#
# plume.rb - Plume!  Plume, plume, plume!
#

require 'rubygems'

require 'eventmachine'
require 'msgpack'
require 'openssl'
require 'socket'

class Plume < EM::Connection
  def public_key
    @key_pub ||= OpenSSL::PKey::RSA.new File.read 'pem/plume.pub'
  end

  def private_key
    @key_priv ||= OpenSSL::PKey::RSA.new File.read 'pem/plume.key'
  end

  def post_init
    start_tls(
      :verify_peer => true,
      :private_key_file => 'pem/plume.key',
      :cert_chain_file => 'pem/plume.crt'
    )
  end

  def ssl_verify_peer(cert)
    true
  end

  def receive_data(data)
    op, data = MessagePack.unpack(data)
    op = op.to_sym

    close_connection if not [:connect, :route].include? op
    send op, *data
  end

  def connect(sig, dst)
    _, username, domain = dst.match(/(.+)@(.+)/).to_a
    puts username, domain
  end

  def method_missing(method, *args, &blk)
    port, ip = Socket.unpack_sockaddr_in(get_peername)
    puts "Plume: #{ip}:#{port} requested invalid op '#{method}'"
  end
end

EM.run { EM.start_server 'localhost', '8080', Plume }
