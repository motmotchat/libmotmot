#
# util.rb - Everybody loves util!
#

require 'dnsruby'
require 'mail'
require 'openssl'

DNSRuby = Dnsruby

#
# Parse an email address string, returning a Mail::Address object on success,
# or nil.
#
def parse_email(s)
  e = Mail::Address.new(s)
  (s == '' || e.address != s || e.local == e.address) ? nil : e
end

#
# Get the address and port of a SRV record.
#
def dns_get_srv(name)
  addr, port = '.', 0

  DNSRuby::DNS.open do |dns|
    srv = dns.getresource(name, DNSRuby::Types.SRV)
    addr, port = srv.target.to_s, srv.port
  end

  (addr == '.' || port == 0) ? [nil, nil] : [addr, port]
end

#
# Extract the common name from an OpenSSL::X509::Certificate.
#
def cert_cn(crt)
  OpenSSL::X509::Name.new(crt.subject).to_a.find { |a| a.first == 'CN' }[1]
end
