#!/usr/bin/env ruby

require_relative './lib'

spawn 5 do |clients|
  clients.each_with_index do |client, i|
    client.write "Hello world! %d\n" % i
  end
  clients.pump
  #clients.kill 2
  clients.spawn 4
  sleep 2
  clients.each_with_index do |client, i|
    client.write "More people %d\n" % i
  end
  clients.pump
end
