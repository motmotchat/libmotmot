# Testing DSL library

require 'fileutils'

ROOT = File.dirname(__FILE__) + '/../'

MOTMOT_PATH = ROOT + 'src/motmot'
CONN_PATH = 'conn/'

class MotMotPool < Array
  def spawn n = 1
    n.times do
      self << IO.popen([MOTMOT_PATH], 'w+')
    end
  end

  def kill n = 1
    # TODO(carl): fix this
    self.sort_by!{rand}.pop(n).each do |p|
      Process.kill 'TERM', p.pid
      p.close
    end
  end

  def kill_all
    self.each do |p|
      Process.kill 'TERM', p.pid
      p.close
    end
    self.replace []
  end

  def pump timeout=1
    loop do
      rs, = IO.select self, [], [], timeout
      return if rs.nil?

      for r in rs
        puts "PID %06d said: %s" % [r.pid, r.gets]
      end
    end
  end
end

module Kernel
  def spawn n
    FileUtils.mkdir_p CONN_PATH

    clients = MotMotPool.new

    begin
      clients.spawn n
      sleep 2 # Wait for initial processes to pick each other up
      yield clients
    ensure
      # Burn them ALL with FIRE
      clients.kill_all
    end
  end
end
