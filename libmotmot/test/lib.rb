# Testing DSL library

require 'fileutils'

ROOT = File.dirname(__FILE__) + '/../'

MOTMOT_PATH = ROOT + 'src/motmot'
CONN_PATH = 'conn/'

module Kernel
  def spawn n
    FileUtils.mkdir_p CONN_PATH
    clients = (1..n).map do
      IO.popen [MOTMOT_PATH], 'w+'
    end
    # Sleep a bit until all the clients pick each other up
    sleep 1.5
    begin
      puts "=" * 80
      yield clients
      puts "=" * 80
    ensure
      # Burn them ALL with FIRE
      clients.map do |p|
        Process.kill 'TERM', p.pid
        p.close
      end
    end
  end
end
