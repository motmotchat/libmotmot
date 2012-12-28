#!/usr/bin/env ruby

require 'eventmachine'
require 'msgpack'

# This connection has a "base" lag
$basedelay = rand(5..300)
$quiet = ARGV[2] == 'quiet'

$all = []

def reschedule q, cb
  EM::next_tick do
    q.pop &cb
  end
end

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
      return if @dieplz

      # Let's sniff the wire
      unless $quiet
        @unpacker.feed data
        @unpacker.each do |obj|
          p obj
        end
      end

      # We want to chunk up the data into random sized chunks. We somewhat
      # arbitrarily choose a geometric distribution with mean 50
      now = Time.now
      data.bytes.slice_before { rand < 1.0 / 50 }.each do |e|
        @nq.push [now, e.pack('c*')]
      end
      reschedule @rq, @rcb
    end

    # Net callback. This is responsible for limiting the data rate and
    # artificially adding latency to the connection
    @ncb = lambda do |data|
      return if @dieplz
      time, data = data
      now = Time.now
      if now - time > @lag
        proxy data
        reschedule @nq, @ncb
      else
        EM::add_timer Time.now - time + (rand / 100) do
          proxy data
          reschedule @nq, @ncb
        end
      end
    end

    # Send queue
    @scb = lambda do |data|
      return if @dieplz
      send_data data
      reschedule @sq, @scb
    end

    init_hook *args
  end
  def receive_data data
    @rq.push data
  end
  def start
    @rq.pop &@rcb
    @nq.pop &@ncb
    @sq.pop &@scb
  end
  def die
    @dieplz = true
    # Wake up all the queues
    @rq.push nil
    @nq.push nil
    @sq.push nil
  end
  def send data
    @sq.push data unless @dieplz
  end
  def unbind
    die
  end
end

# The binding (server) side of the proxy
module BindProxy
  include DelayProxy
  def init_hook other
    $all << self
    @other = other
  end
  def post_init
    EM::connect @other, nil, RespProxy, self, @lag
  end
  def register resp
    @resp = resp
    start
  end
  def proxy data
    @resp.send data
  end
  def unbind
    die
    @resp.die
    $all.delete self
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
  def unbind
    die
    @bind.die
  end
  def type
    'RESP'
  end
end

def kill_things
  $all[rand($all.length)].close_connection if $all.length > 0 and rand < 0.1
end

EM::run do
  # Ruby does ARGV funny
  EM::start_server ARGV[0], nil, BindProxy, ARGV[1]

  EM::add_periodic_timer 0.2 do
    kill_things
  end
end
