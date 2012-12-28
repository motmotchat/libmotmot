# Testing DSL library

require 'fileutils'

ROOT = File.dirname(__FILE__) + '/../'

MOTMOT_PATH = ROOT + 'src/motmot'
CONN_PATH = 'conn/'
PROXY_PATH = ROOT + 'test/netproxy.rb'

class MotMot
  attr_reader :unix, :sock

  def initialize connect=[]
    @unix = CONN_PATH + rand(100000).to_s
    #@proxy = IO.popen [PROXY_PATH, proxy, unix, 'quiet'], 'w+'
    @sock = IO.popen ([MOTMOT_PATH, unix] + connect), 'w+'
    #sleep 0.1
    # XXX: this is gross
    `echo attach #{pid} > /tmp/#{pid}`
    `echo continue >> /tmp/#{pid}`
    `screen gdb -x /tmp/#{pid}`
  end

  def kill
    FileUtils.rm_f "/tmp/#{pid}"
    Process.kill 'TERM', @sock.pid
    Process.kill 'TERM', @proxy.pid
    @sock.close
    @proxy.close
    FileUtils.rm_f unix
    FileUtils.rm_f proxy
  end

  def proxy
    @unix# + 'p'
  end

  # Proxy all other methods to the socket
  def method_missing method, *args, &block
    @sock.send method, *args, &block
  end
end

class MotMotPool < Hash
  attr_reader :reaper
  def initialize n=1
    n.times do
      spawn
      sleep 0.7
    end
    @reaper = Thread.new do
      puts "REAPER"
      output = []
      offsets = Hash.new 0
      begin
        loop do
          sockets = values.compact.map &:sock
          sockets.delete_if &:closed?
          break unless sockets.size
          rs, = IO.select sockets, [], [], 10
          break if rs.nil?

          for r in rs
            chat = r.gets
            if chat.nil?
              delete r.pid
              next
            end
            puts chat
            if chat.match /^CHAT/
              print '.'
              print "\n" if rand < 0.001
              output[offsets[r.pid]] ||= chat
              if output[offsets[r.pid]] != chat
                puts "OUT OF ORDER CHAT! PID: #{r.pid}, CHAT: #{chat}"
                kill_all
              end
              offsets[r.pid] += 1
            end
          end
        end
      rescue
        # Do nothing
      end
    end
  end
  def spawn
    if size == 1
      # If there's only one person in the pool, make the new instance the
      # president and connect to the existing instance
      _, old = self.first
      m = MotMot.new [old.proxy]
    else
      m = MotMot.new
      if size > 1
        # Tell a random person to invite us
        whom = keys[rand size]
        sleep 0.1
        self[whom].write "/invite #{m.proxy}\n"
      end
    end
    self[m.pid] = m
  end

  def kill_all
    self.values.each &:kill
  end

  def random
    self[keys[rand size]]
  end

  def kill
    k = keys[rand size]
    k.kill
    delete k
  end
end

module Kernel
  def spawn n=4
    FileUtils.mkdir_p CONN_PATH

    clients = MotMotPool.new n

    begin
      yield clients
    ensure
      # Kill them all with FIRE
      clients.reaper.join
      clients.kill_all
      #clients.reaper.raise "get outta there"
    end
  end
end
