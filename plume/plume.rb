#
# plume.rb - Plume!  Plume, plume, plume!
#

require 'rubygems'

require 'eventmachine'
require 'msgpack'
require 'openssl'

require_relative 'plume_em.rb'

class Plume < PlumeEM

  KEY_FILE = 'pem/plume.key'
  CRT_FILE = 'pem/plume.crt'
  LEGAL_OPS = %w(connect route)

  def ssl_verify_peer(cert)
    true
  end

  private

  def connect(sig, dst)
    _, username, domain = dst.match(/(.+)@(.+)/).to_a
    puts username, domain
  end
end

EM.run { EM.start_server 'localhost', '9000', Plume }
