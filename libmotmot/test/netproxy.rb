#!/usr/bin/env ruby

require 'eventmachine'
require 'msgpack'

# This connection has a "base" lag
$basedelay = rand(5..300)
$quiet = ARGV[2] == 'quiet'

module DelayProxy
  def initialize *args
    # First, how many milliseconds (ish) should we delay by?
    unless @lag
      @lag = $basedelay + rand(0..100)
      @lag = @lag / 1000.0
    end

    # Send, net, and recieve queues
    @sq = EM::Queue.new
    @nq = EM::Queue.new
    @rq = EM::Queue.new

    @unpacker = MessagePack::Unpacker.new

    # Recieve callback
    @rcb = lambda do |data|
      # We push a nil "bubble" through the pipeline to indicate that we should
      # close it
      return @nq.push nil if data == nil

      # Let's sniff the wire
      @unpacker.feed data
      @unpacker.each do |obj|
        p obj unless $quiet
      end

      # We want to chunk up the data into random sized chunks. We somewhat
      # arbitrarily choose a geometric distribution with mean 50
      now = Time.now
      data.bytes.slice_before { rand < 1.0 / 50 }.each do |e|
        @nq.push [now, e.pack('c*')]
      end
      # Schedule ourselves again
      EM::next_tick do
        @rq.pop &@rcb
      end
    end

    # Net callback. This is responsible for limiting the data rate and
    # artificially adding latency to the connection
    @ncb = lambda do |data|
      return proxy nil if data == nil
      time, data = data
      now = Time.now
      if now - time > @lag
        proxy data
        EM::next_tick do
          @nq.pop &@ncb
        end
      else
        EM::add_timer Time.now - time + (rand / 100) do
          proxy data
          EM::next_tick do
            @nq.pop &@ncb
          end
        end
      end
    end

    # Send queue
    @scb = lambda do |data|
      return close_connection if data == nil
      send_data data
      EM::next_tick do
        @sq.pop &@scb
      end
    end

    init_hook *args
  end
  def post_init
    # Start up the net and send queues
    @nq.pop &@ncb
    @sq.pop &@scb

    post_init_hook
  end
  def receive_data data
    @rq.push data
  end
  def start
    @rq.pop &@rcb
  end
  def send data
    @sq.push data unless @unbound
  end
  def unbind
    unless @unbound
      @unbound = true
      p 'unbind' unless $quiet
      p type unless $quiet
      @rq.push nil
    end
  end

  # Stuff to override
  def post_init_hook
  end
  def proxy data
    send_data data
  end
end

# The binding (server) side of the proxy
module BindProxy
  include DelayProxy
  def init_hook other
    @other = other
  end
  def post_init_hook
    EM::connect @other, nil, RespProxy, self, @lag
  end
  def register resp
    @resp = resp
    start
  end
  def proxy data
    @resp.send data
  end
  def type
    'BIND'
  end
end

# A proxy that corresponds to a bind instance, and handles the replies
module RespProxy
  include DelayProxy
  def init_hook bind, lag
    @bind = bind
    @bind.register self
    @lag = lag
    start
  end
  def proxy data
    @bind.send data
  end

  def type
    'RESP'
  end
end

def kill_things
  p 'die!'
end

EM::run do
  # Ruby does ARGV funny
  EM::start_server ARGV[0], nil, BindProxy, ARGV[1]

  #EM::add_periodic_timer 1 do
  #  kill_things
  #end
end
