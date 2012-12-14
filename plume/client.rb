#
# client.rb - Plume client.
#

require 'rubygems'

require 'eventmachine'
require 'highline/import'
require 'readline'

require_relative 'conn/client_login.rb'
require_relative 'conn/client_plume.rb'
require_relative 'util.rb'

USAGE = "\nUsage:\n" + <<-eos
    [h]elp              Show usage instructions.
    [c]onnect PEER      Establish a connection with PEER.
    [q]uit              End the session.\n
eos

@server = ARGV[0] || 'localhost'
@key_file = 'pem/client.key'
@crt_file = 'pem/client.crt'

def prompt(conn, msg=nil)
  puts msg unless msg.nil?

  EM.next_tick do
    unless buf = Readline.readline('> ', true)
      conn.close_connection
      abort "\n"
    end

    input = buf.strip.split
    msg = nil

    case input[0]
    when nil
    when 'connect', 'c'
      if input[1].nil?
        msg = "Must specify a peer to connect to."
      else
        conn.connect input[1]
      end
    when 'help', 'h', '?'
      msg = USAGE
    when 'exit', 'quit', 'q'
      conn.close_connection
      exit
    else
      msg = "Invalid command.  Type 'h' for help."
    end

    prompt conn, msg
  end
end

def conn_plume
  identity = cert_cn(OpenSSL::X509::Certificate.new File.read @crt_file)

  # Validate the login certificate.
  email = parse_email(identity)
  abort "Invalid login." if email.nil?

  # Server DNS lookup.
  addr, port = dns_get_srv("_plume._tcp.#{email.domain}")

  # Default to localhost for testing.
  addr, port = 'localhost', 42000 if addr.nil? or port.nil?

  # Connect to the server.
  EM.connect(addr, port, ClientPlumeConn, @key_file, @crt_file) do |conn|
    prompt conn
  end
end

def conn_login
  # Get the user's identity handle and password.
  loop do
    identity = ask('Identity: ') { |q| q.echo = true }
    email = parse_email(identity)

    break if not email.nil?
    puts "Please try again."
  end
  password = ask('Password: ') { |q| q.echo = false }

  # Determine the address and port of the peer's Plume server.
  addr, port = dns_get_srv("_plume-login._tcp.#{email.domain}")

  # Default to localhost for testing.
  addr, port = 'localhost', 42001 if addr.nil? or port.nil?

  # Login with the server.
  EM.connect(addr, port, ClientLoginConn, @key_file, @crt_file) do |conn|
    conn.login identity, password, &method(:plume)
  end
end

EM.run { if File.exists? @crt_file then conn_plume else conn_login end }
