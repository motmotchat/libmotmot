#
# client.rb - Plume client.
#

require 'rubygems'

require 'eventmachine'
require 'highline/import'
require 'msgpack'
require 'openssl'
require 'readline'

require_relative 'conn.rb'

SERVER = ARGV[0] || 'localhost'
CLIENT_CRT = 'pem/client.crt'

#
# EventMachine client for the Plume login server.
#
class LoginClient < PlumeConn

  KEY_FILE = 'pem/client.key'
  CRT_FILE = CLIENT_CRT
  LEGAL_OPS = %w(login)
  OP_PREFIX = 'recv_'

  ERROR_MSG = "PlumeClient: An error occurred.  Please try again."

  def post_init
    start_tls(:verify_peer => true)
  end

  def ssl_handshake_completed; login end

  def unbind
    abort ERROR_MSG if error?
  end

  private

  #
  # Generate a CSR and send it to the login server.
  #
  def login
    username = ask('Username: ') { |q| q.echo = true }
    password = ask('Password: ') { |q| q.echo = false }

    name = OpenSSL::X509::Name.parse "CN=#{username}@#{SERVER}"

    csr = OpenSSL::X509::Request.new
    csr.version = 0
    csr.subject = name
    csr.public_key = key.public_key

    csr.sign key, OpenSSL::Digest::SHA1.new

    send_data ['login', [csr.to_pem]].to_msgpack
  end

  #
  # Receive a signed cert from the login server; on success, write it to disk
  # and connect to the Plume server.
  #
  def recv_login(csr_cert)
    abort ERROR_MSG if csr_cert.nil?

    File.open(CRT_FILE, 'w') { |f| f.write(csr_cert) }
    close_connection

    EM.connect SERVER, 9000, PlumeClient
  end
end

#
# Plume client.
#
class PlumeClient < PlumeConn

  USAGE = "\nUsage:\n" + <<-eos
    [h]elp              Show usage instructions.
    [c]onnect PEER      Establish a connection with PEER.
    [q]uit              End the session.\n
  eos

  KEY_FILE = 'pem/client.key'
  CRT_FILE = CLIENT_CRT
  LEGAL_OPS = %w(connect pubkey)
  OP_PREFIX = 'recv_'

  def initialize
    super()
    @peers = {}
  end

  def ssl_handshake_completed; prompt end

  def unbind
    puts "PlumeClient: An error occurred." if error?
  end

  private

  def prompt(msg=nil)
    puts msg unless msg.nil?
    EM.next_tick &method(:_prompt)
  end

  def _prompt
    unless buf = Readline.readline('> ', true)
      close_connection
      abort "\n"
    end

    input = buf.strip.split

    case input[0]
    when 'connect', 'c'
      if input[1].nil? or parse_email(input[1]).nil?
        prompt "Must specify a valid peer to connect to."
      else
        connect input[1]
      end
    when 'help', 'h', '?'
      prompt USAGE
    when 'exit', 'quit', 'q'
      close_connection
      exit
    else
      prompt "Invalid command.  Type 'h' for help."
    end
  end

  #
  # Request information from the Plume server to connect to peer.
  #
  def connect(peer)
    @peers[peer] = true

    ip, port = get_sockaddr
    cc = OpenSSL::PKCS7.sign(cert, key, [cert.to_pem, ip, port].to_msgpack)

    send_data [
      'route',
      [cert.to_pem, peer, 'connect', cc.to_pem]
    ].to_msgpack

    prompt
  end

  #
  # Receive a connection request from a peer.
  #
  def recv_connect(peer_cert, _, payload)
  end
end

EM.run {
  if File.exists? CLIENT_CRT
    EM.connect SERVER, 9000, PlumeClient
  else
    EM.connect SERVER, 9001, LoginClient
  end
}
