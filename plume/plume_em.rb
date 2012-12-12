#
# plume_em.rb - EventMachine::Connection object for Plume agents.
#

require 'eventmachine'
require 'mail'
require 'msgpack'
require 'openssl'
require 'socket'

class PlumeEM < EM::Connection

  KEY_FILE = nil
  CRT_FILE = nil
  LEGAL_OPS = []
  OP_PREFIX = ''

  attr_reader :peer_handle

  #
  # Initiate TLS by default.
  #
  def post_init
    start_tls(
      :verify_peer => true,
      :private_key_file => self.class::KEY_FILE,
      :cert_chain_file => self.class::CRT_FILE
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

  def receive_data(data)
    op, data = MessagePack.unpack(data)

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
    @key ||= OpenSSL::PKey::RSA.new File.read self.class::KEY_FILE
  end

  def cert
    @cert ||= OpenSSL::X509::Certificate.new File.read self.class::CRT_FILE
  end

  #
  # Return the IP address and port of our peer.
  #
  def get_peeraddr
    Socket.unpack_sockaddr_in(get_peername).reverse
  end

  #
  # Parse an email address string, returning a Mail::Address object on success,
  # or nil.
  #
  def parse_email(s)
    e = Mail::Address.new(s)
    (s == '' || e.address != s || e.local == e.address) ? nil : e
  end
end
