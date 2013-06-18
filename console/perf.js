

register_command('perf', {
    invoke: function(args) {
        args.shift()
        var cmd = args.shift()
        if (cmd in this.subcommands) {
            this.subcommands[cmd].invoke(args)
        } else {
            this.help([])
        }
    },
    help: function(args) {
        write_string('usage:\n\n')
        for (var k in this.subcommands) {
            write_string('  perf ' + this.subcommands[k].usage + '\n')
        }
    },
    list: function(args) {
        write_string('available tracpoints:\n\n')
        trace = Packages.com.cloudius.trace
        all = trace.Tracepoint.list()
        for (var i = 0; i < all.size(); ++i) {
            var tp = all.get(i)
            write_string('    ' + String(tp.getName()))
            write_char('\n')
        }
    },
    stat: function(args) {
        var pkg = Packages.com.cloudius.trace
        var counters = []
        for (var i in args) {
            var x = args[i]
            var m = /^(([^=]+)=)?(.+)$/.exec(x)
            var tag = m[2]
            var name = m[3]
            if (!tag) {
                tag = name
            }
            try {
                var tp = new pkg.Tracepoint(name)
                var counter = new pkg.Counter(tp)
                counters.push({
                    tag: tag,
                    counter: counter,
                    width: Math.max(8, tag.length + 2),
                    last: 0,
                })
            } catch (err) {
                write_string('bad tracepoint "' + name + '"\n')
                return
            }
        }
        var titles = function() {
            for (var i in counters) {
                var c = counters[i]
                for (var j = c.tag.length; j < c.width; ++j) {
                    write_string(' ')
                }
                write_string(c.tag)
            }
            write_string('\n')
        }
        var show = function() {
            for (var i in counters) {
                var ctr = counters[i]
                var last = ctr.last
                ctr.last = ctr.counter.read()
                var delta = ctr.last - last
                delta = delta.toString()
                for (var j = delta.length; j < ctr.width; ++j) {
                    write_string(' ')
                }
                write_string(delta)
            }
            write_string('\n')
        }
        var line = 0
        while (true) {
            if (line++ % 25 == 0) {
                titles()
            }
            show()
            flush()
            java.lang.Thread.sleep(1000)
        }
    },
    subcommands: {
        list: {
            invoke: function(args) { this.parent.list(args) },
            usage: 'list',
        },
        stat: {
            invoke: function(args) { this.parent.stat(args) },
            usage: 'stat [[tag=]tracepoint]...',
        },
    },
    init: function() {
        for (var k in this.subcommands) {
            this.subcommands[k].parent = this
        }
    },
})

