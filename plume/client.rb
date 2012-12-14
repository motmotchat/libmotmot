#
# client.rb - Plume client.
#

require 'rubygems'

require 'eventmachine'
require 'highline/import'
require 'readline'

require_relative 'conn/client_login.rb'
require_relative 'conn/client_plume.rb'

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

def plume
  EM.connect(@server, 9000, ClientPlumeConn, @key_file, @crt_file) do |conn|
    prompt conn
  end
end

def login
  EM.connect(@server, 9001, ClientLoginConn, @key_file, @crt_file) do |conn|
    conn.login &method(:plume)
  end
end

EM.run { if File.exists? @crt_file then plume else login end }
