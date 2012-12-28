#!/usr/bin/env ruby

require_relative './lib'

spawn 5 do |clients|
  clients.each do |unix, client|
    client.write "Hello world! #{unix} #{client.pid}\n"
    client.flush
  end
  sleep 1
end
