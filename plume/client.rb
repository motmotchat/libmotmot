require 'eventmachine'
require 'msgpack'

class PlumeClient < EM::Connection
  def connection_completed
    start_tls(
      :verify_peer => true,
      :private_key_file => 'pem/client.key',
      :cert_chain_file => 'pem/client.crt'
    )
  end

  def ssl_verify_peer(cert)
    true
  end

  def ssl_handshake_completed
    send_data ['connect', [nil, 'client@localhost']].to_msgpack
  end

  def receive_data(data)
    puts data
  end
end

EM.run { EM.connect 'localhost', 8080, PlumeClient }
