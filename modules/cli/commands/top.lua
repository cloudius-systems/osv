local OptionParser = require('std.optparse')
local socket = require('socket')

-- socket lib to the rescue
local function sleep(sec)
  socket.select(nil, nil, sec)
end

local function time()
  return socket.gettime()
end

local columns = {
  ["ID"] = {
    name = "ID",
    source = "id"
  },
  ["CPU"] = {
    name = "CPU",
    source = "cpu"
  },
  ["%CPU"] = {
    name = "%CPU",
    format = "%.1f",
    source = "cpu_ms",
    rate = true,
    multiplier = 0.1
  },
  ["TIME"] = {
    name = "TIME",
    format = "%.2f",
    source = "cpu_ms",
    multiplier = 0.001
  },
  ["NAME"] = {
    name = "NAME",
    source = "name"
  },
  ["sw"] = {
    name = "sw",
    source = "switches",
  },
  ["sw/s"] = {
    name = "sw/s",
    format = "%.1f",
    source = "switches",
    rate = true,
  },
  ["us/sw"] = {
    name = 'us/sw',
    format = '%.0f',
    source = 'cpu_ms',
    multiplier = 1000.0,
    rateby = 'switches'
  },
  ["preempt"] = {
    name = 'preempt',
    source = 'preemptions',
  },
  ["pre/s"] = {
    name = 'pre/s',
    format = '%.1f',
    source = 'preemptions',
    rate = true,
  },
  ["mig"] = {
    name = 'mig',
    source = 'migrations',
  },
  ["mig/s"] = {
    name = 'mig/s',
    format = '%.1f',
    source = 'migrations',
    rate = true,
  },
}

local clear_page = '\27[H\27[2J'

local cmd = {}

cmd.desc = [[display OSv threads]]

cmd.parser = OptionParser [[
top

Usage: top

Show thread information

Options:

  -l, --lines=[LINES]     Force number of lines to display (default: try to
                          fit to display)
  -i, --idle              Show idle threads in list
  -s, --switches          Show switches
  -p, --period=[SECONDS]  Refresh interval (in seconds)
  -h, --help              Display this help and exit
]]

cmd.help_flags = {"-h", "--help"}

cmd.main = function(args)
  local args, opts = cmd.parser:parse(args)

  local cli_height, cli_width = cli_console_dim()
  local thread_limit = cli_height
  if opts.lines then thread_limit = tonumber(opts.lines) end

  local show_idle = opts.idle

  local cols = {"ID", "CPU", "%CPU", "TIME", "NAME"}
  if opts.switches then
    cols = {"ID", "CPU", "%CPU", "TIME", "sw", "sw/s", "us/sw", "preempt",
            "pre/s", "mig", "mig/s", "NAME"}
  end

  local interval = 2
  if opts.period then
    interval = opts.period
  end

  -- Column header
  local thead = cols

  -- CPU count
  local cpu_count, status = osv_request("/hardware/processor/count")
  osv_resp_assert(status, 200)

  local prev = nil
  local current = {time_ms = 0, list = {}}
  local prev_i = nil
  local current_i = {}

  -- Sorts threads by cpu_ms
  local function sort_threads(a, b)
    local cur_a_ms = current.list[a].cpu_ms
    local cur_b_ms = current.list[b].cpu_ms

    -- If it's a new thread, take 0 for its previous cpu_ms
    local pre_a_ms = prev and prev.list[a] and prev.list[a].cpu_ms or 0
    local pre_b_ms = prev and prev.list[b] and prev.list[b].cpu_ms or 0

    return cur_a_ms - pre_a_ms > cur_b_ms - pre_b_ms
  end

  -- TODO: Stop somehow
  while true do
    local start_time_ms = time()
    local threads, status = osv_request("/os/threads", "GET")
    osv_resp_assert(status, 200)

    -- Save the thread IDs (Lua's table.sort works on "array"-tables only)
    local thread_ids = {}

    -- Organize
    current.time_ms = threads.time_ms
    for i = 1, #threads.list do
      local thread = threads.list[i]
      local is_idle = string.find(thread.name, "idle") == 1

      -- Add to idle threads
      if is_idle then
        current_i[thread.id] = thread
      end

      if is_idle and not show_idle then
        -- Skip; Condition more understandable ;-)
      else
        current.list[thread.id] = thread
        table.insert(thread_ids, thread.id)
      end

      -- Thread without CPU, yet
      if thread.cpu == 0xffffffff then
        thread.cpu = '-'
      end
    end

    -- Sort
    table.sort(thread_ids, sort_threads)

    -- Global thread status (top-most line)
    local thread_status = {#threads.list, " threads on ", cpu_count, " CPUs"}
    if prev_i and prev then
      -- Idle status
      local idles = {}
      local total = 0

      for id in pairs(current_i) do
        local idle = 100 * (current_i[id].cpu_ms - prev_i[id].cpu_ms) /
                           (current.time_ms - prev.time_ms)
        total = total + idle
        idles[current_i[id].cpu+1] = string.format("%.0f%%", idle)
      end
      table.insert(thread_status, "; ")
      table.insert(thread_status, table.concat(idles, " "))
      table.insert(thread_status, string.format(" %.0f%%", total))
    end
    thread_status = table.concat(thread_status) .. "\n"

    -- Build the list to be printed
    local tt = {thead}
    for _, id in ipairs(thread_ids) do
      local tline = {}
      for _, col in ipairs(cols) do
        local column = columns[col]

        local val = current.list[id][column.source]
        if column.rate and prev then
          val = val - prev.list[id][column.source]
          val = val / ((current.time_ms - prev.time_ms) / 1000)
        elseif column.rateby and prev then
          val = val - prev.list[id][column.source]
          if val ~= 0 then
            val = val / (current.list[id][column.rateby] -
                         prev.list[id][column.rateby])
          end
        end

        if column.multiplier then
          val = val * column.multiplier
        end

        if column.format then
          val = string.format(column.format, val)
        else
          val = tostring(val)
        end

        table.insert(tline, val)
      end
      table.insert(tt, tline)

      -- Break if more threads then possible to fit in the limit
      -- One header line in tt, plus the thread_status line (which might be
      -- wider than screen)
      if #tt >= thread_limit - (string.len(thread_status) / cli_width + 1) then
        break
      end
    end

    -- Print to screen
    io.write(clear_page)
    io.write(thread_status)
    io.write(table_format(tt))

    prev = current
    current = {time_ms = 0, list = {}}
    prev_i = current_i
    current_i = {}
    sleep(math.max(0, (interval - (time() - start_time_ms))))
  end
end

return cmd
