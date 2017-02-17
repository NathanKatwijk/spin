#!/usr/bin/env lua
--- lua websocket equivalent to test-server.c from libwebsockets.
-- using copas as server framework

package.path = '../src/?.lua;../src/?/?.lua;'..package.path
local copas = require'copas'
local socket = require'socket'
local P = require'posix'

local traffic_clients = {}
local info_clients = {}
local websocket = require'websocket'

--
-- The traffic data protocol send out continuous updates of
-- traffic data as collected by the collector module
--
-- The info protocol is a simple async request response protocol
-- requests are in the form
-- <type> <request data>
-- and responses in the form
-- <type> <request data> <response data>
-- example:
-- Request: arp 192.168.1.1
-- Response: arp 192.168.1.1 aa:bb:cc:dd:ee:ff


local server = websocket.server.copas.listen
{
  protocols = {
    ['traffic-data-protocol'] = function(ws)
      traffic_clients[ws] = 0
      while true do
        local message,opcode = ws:receive()
        if not message then
          ws:close()
          traffic_clients[ws] = nil
          return
        end
        if opcode == websocket.TEXT then
          if message:match('reset') then
            traffic_clients[ws] = 0
          end
        end
      end
    end,
    ['info-protocol'] = function(ws)
      while true do
        local msg,opcode = ws:receive()
        if not msg then
          ws:close()
          return
        else
          response = msg .. " bar"
          ws:send(resposne)
        end
        --if opcode == websocket.TEXT then
        --  request
        --  ws:send(response)
        --end
      end
    end
  },
  port = 12345
}

local pid_file_name = "/tmp/TESTPIDFILE.pid"
function remove_pidfile()
    print("yo")
    --os.remove(pid_file_name)
    os.exit()
end

-- TODO: move what we can to the module

function test_print(msg)
  print(msg)
end

function create_callback(ic)
  return function(msg)
    for ws,number in pairs(ic) do
      --ws:send(msg)
      ws:send(msg)
      ic[ws] = number + 1
    end
  end
end

function send_number_loop()
    local last = socket.gettime()
    while true do
      copas.step(0.1)
      local now = socket.gettime()
      if (now - last) >= 0.1 then
        last = now
        for ws,number in pairs(traffic_clients) do
          ws:send(tostring(number))
          traffic_clients[ws] = number + 1
        end
      end
    end
end

function collector_loop()
    local fd1 = P.open("/tmp/spin_pipe", bit.bor(P.O_RDONLY, P.O_NONBLOCK))
    --local fd2 = P.open(arg[2], P.O_RDONLY)

    cb = create_callback(traffic_clients)
    --cb = test_print
    while true do
        --print("collector loop")
        copas.step(0.1)
        handle_pipe_output(fd1, cb)
    end
end

local collector = require'collect'

print("add thread")
copas.addthread(
collector_loop
)
print("add thread done")


copas.loop()
