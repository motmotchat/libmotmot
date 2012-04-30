#!/usr/bin/env ruby

require_relative './lib'

spawn 5 do |clients|
  sleep 1
  100000.times do |i|
    client = clients.random
    client.write "Message number #{i}\n"
    client.flush
  end
  sleep 1
end
