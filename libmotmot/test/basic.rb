#!/usr/bin/env ruby

require_relative './lib'

spawn 5 do |clients|
  clients.each_with_index do |client, i|
    client.write "Hello world! %d\n" % i
  end
  loop do
    rs, = IO.select clients, [], [], 1
    break if rs.nil?

    for r in rs
      puts "PID %06d said: %s" % [r.pid, r.gets]
    end
  end
end
