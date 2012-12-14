#
# plume_em.rb - EventMachine::Connection object for Plume agents.
#

require 'eventmachine'
require 'msgpack'
require 'openssl'
require 'socket'

class PlumeConn < EM::Connection

  attr_reader :key_file, :crt_file

  LEGAL_OPS = []
  OP_PREFIX = ''

  def initialize(key_file, crt_file)
    super()

    @key_file = key_file
    @crt_file = crt_file
    @buffer = ''
  end

  #
  # Initiate TLS by default.
  #
  def post_init
    start_tls(
      :verify_peer => true,
      :private_key_file => key_file,
      :cert_chain_file => crt_file
    )
  end

  #
  # Verify our peer's cert.
  #
  def ssl_verify_peer(cert)
    cert = OpenSSL::X509::Certificate.new cert

    #cert.verify cert.public_key
    true
  end

  def receive_data(buf)
    @buffer += buf

    begin
      op, data = MessagePack.unpack(@buffer)
    rescue MessagePack::UnpackError
      return
    end
    @buffer = ''

    return close_connection unless self.class::LEGAL_OPS.include? op

    send (self.class::OP_PREFIX + op).to_sym, *data
  end

  def method_missing(m, *args, &blk)
    m = m.to_s

    if m.start_with? self.class::OP_PREFIX
      op = m[(self.class::OP_PREFIX.size)..-1]
      ip, port = get_peeraddr

      puts "Plume: #{ip}:#{port} requested invalid op `#{op}'"
    else
      raise NoMethodError, "undefined method `#{m}' for #{inspect}"
    end
  end

  private

  def key
    @key ||= OpenSSL::PKey::RSA.new File.read key_file
  end

  def cert
    @cert ||= OpenSSL::X509::Certificate.new File.read crt_file
  end

  #
  # Return our own IP and port.
  #
  def get_sockaddr
    Socket.unpack_sockaddr_in(get_sockname).reverse
  end

  #
  # Return the IP address and port of our peer.
  #
  def get_peeraddr
    Socket.unpack_sockaddr_in(get_peername).reverse
  end
end
