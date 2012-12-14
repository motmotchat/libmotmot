#
# plume_udp.rb - Plume UDP echo service listener.
#

require 'eventmachine'
require 'msgpack'

class PlumeUDPEcho < EM::Connection

  def initialize(udp_reqs)
    @udp_reqs = udp_reqs
  end

  def receive_data(buf)
    begin
      cookie = MessagePack.unpack(buf)
    rescue MessagePack::UnpackError
    end

    return unless @udp_reqs[cookie]

    # Send the client's IP and port back via TCP, then clear the cookie.
    @udp_reqs[cookie].send_data ['udp', [cookie, get_peeraddr]].to_msgpack
    @udp_reqs.delete cookie
  end

  def get_peeraddr
    Socket.unpack_sockaddr_in(get_peername).reverse
  end
end
