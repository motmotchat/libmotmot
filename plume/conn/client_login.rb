#
# client_login.rb - Connection between a client and a login server.
#

require 'msgpack'
require 'openssl'

require_relative 'conn.rb'

class ClientLoginConn < PlumeConn

  LEGAL_OPS = %w(login)
  OP_PREFIX = 'recv_'

  ERROR_MSG = "PlumeClient: An error occurred.  Please try again."

  def post_init
    start_tls(:verify_peer => true)
  end

  def unbind
    abort ERROR_MSG if error?
  end

  #
  # Generate a CSR and send it to the login server.
  #
  def login(&blk)
    @cb = blk

    identity = ask('Identity: ') { |q| q.echo = true }
    password = ask('Password: ') { |q| q.echo = false }

    name = OpenSSL::X509::Name.parse "CN=#{identity}"

    csr = OpenSSL::X509::Request.new
    csr.version = 0
    csr.subject = name
    csr.public_key = key.public_key

    csr.sign key, OpenSSL::Digest::SHA1.new

    send_data ['login', [csr.to_pem]].to_msgpack
  end

  private

  #
  # Receive a signed cert from the login server; on success, write it to disk
  # and connect to the Plume server.
  #
  def recv_login(csr_cert)
    abort ERROR_MSG if csr_cert.nil?

    File.open(crt_file, 'w') { |f| f.write(csr_cert) }
    close_connection

    @cb.call if @cb
  end
end
