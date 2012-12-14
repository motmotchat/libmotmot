#
# login.rb - Plume login server.
#

require 'rubygems'
require 'bundler/setup'

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'conn/conn.rb'

class PlumeLogin < PlumeConn

  LEGAL_OPS = %w(login)

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

plume_dir = File.expand_path "~/.plume/#{ENV['PLUME_HOSTNAME'].to_s}"
key_file = ENV['PLUME_LOGIN_KEY'] || plume_dir + '/login.key'
crt_file = ENV['PLUME_LOGIN_CRT'] || plume_dir + '/login.crt'

port = ARGV[0] || '42001'

EM.run { EM.start_server '0.0.0.0', port, PlumeLogin, key_file, crt_file }
