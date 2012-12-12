#
# login.rb - Plume login server.
#

require 'rubygems'

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'plume_em.rb'

class PlumeLogin < PlumeEM

  KEY_FILE = 'pem/login.key'
  CRT_FILE = 'pem/login.crt'
  LEGAL_OPS = %w(login)

  def post_init
    start_tls(
      :verify_peer => true,
      :private_key_file => KEY_FILE,
      :cert_chain_file => CRT_FILE
    )
  end

  def ssl_verify_peer(cert)
    true
  end

  def login(csr)
    csr = OpenSSL::X509::Request.new csr

    return close_connection if not csr.verify csr.public_key

    csr_cert = OpenSSL::X509::Certificate.new
    csr_cert.serial = 0
    csr_cert.version = 2
    csr_cert.not_before = Time.now
    csr_cert.not_after = Time.now + 600

    csr_cert.subject = csr.subject
    csr_cert.public_key = csr.public_key
    csr_cert.issuer = cert.subject

    extension_factory = OpenSSL::X509::ExtensionFactory.new
    extension_factory.subject_certificate = csr_cert
    extension_factory.issuer_certificate = cert

    extension_factory.create_extension 'basicConstraints', 'CA:FALSE'
    extension_factory.create_extension 'keyUsage',
        'keyEncipherment,dataEncipherment,digitalSignature'
    extension_factory.create_extension 'subjectKeyIdentifier', 'hash'

    csr_cert.sign key, OpenSSL::Digest::SHA1.new
    send_data ['login', [csr_cert.to_pem]].to_msgpack
  end
end

EM.run { EM.start_server 'localhost', '9001', PlumeLogin }
