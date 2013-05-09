importPackage(java.io);
importPackage(java.lang);

load("/console/util.js");
load("/console/autocomplete.js");
load("/console/cd.js");
load("/console/pwd.js");
load("/console/ls.js");
load("/console/cat.js");
load("/console/help.js");
load("/console/tests.js");
load("/console/run.js");
load("/console/ifconfig.js");

// Commands
var _commands = new Array();
_commands["ls"] = ls;
_commands["cd"] = cd;
_commands["pwd"] = pwd;
_commands["cat"] = cat;
_commands["help"] = help_cmd;
_commands["test"] = test_cmd;
_commands["run"] = run_cmd;
_commands["ifconfig"] = ifconfig_cmd;

// Create interface to networking functions
var networking_interface = new Networking();

// I/O
var _reader = new BufferedReader( new InputStreamReader(System['in']) );
var _writer = new BufferedWriter( new OutputStreamWriter(System['out']));

// Line
var _line_idx = 0;
var _line = new Array();
var _c = 0;

/* FIXME: Handle multi-line commands */
var MAX_LENGTH = 80;

// Prompt
_prompt = "[osv]$ ";
function update_prompt()
{
    var cwd = cd.pwd();
    _prompt = "[" + cwd + "]$ ";
}

function flush()
{
    _writer.flush();
}

function write_char(c)
{
    _writer.write(c);
}

function write_string(s)
{
    if (s.length == 0) {
        return;
    }
    
    _writer.write(s, 0, s.length);
}

function beep()
{
    write_char('\7');
    flush();
}

function read_line()
{
    var line = "";
    for (var i=0; i<MAX_LENGTH; i++) {
        if ((_line[i] == 0) || (_line[i] == 13))
            break;
        
        line += chr(_line[i]);
    }
    
    return (line);
}

// Return an array of words from the input
function input()
{
    var line = read_line();
    if (line == "") {
        return (new Array());
    }
    
    line = line.replace(/ +$/,'');
    return (line.split(" "));
}

// Returns true if word completed
function completed_word()
{
    if (_line_idx == 0) {
        return (false);
    }
    
    if (_line[_line_idx-1] == 0x20) {
        return (true);
    }
    
    return (false);
}

function render(newline)
{
    var cooked = "";
    var line = read_line();
    
    cooked += "\r" + _prompt + line;
    if (newline) {
        cooked += "\n";
    }
    
    write_string(cooked);
    flush();
}

function reset_line()
{
    for (var i=0; i<MAX_LENGTH; i++) {
        _line[i] = 0;
    }
    
    _line_idx = 0;
    _c = 0;
}

function backspace()
{
    if (_line_idx == 0) {
        beep();
        return;
    }
    
    write_string("\b ");
    _line[--_line_idx] = 0;
}

function tab()
{
    var inp = input();
    
    // Try to find a top level command
    if (inp.length == 0) {
        get_suggestions(_command_names, "");
        return;
    } else if ((inp.length == 1) && (!completed_word())) {
        get_suggestions(_command_names, inp[0]);
        return;
    }
    
    // Look for command
    if (inp[0] in _commands) {
        var cmd = _commands[inp[0]];
        if (cmd.tab) {
            var results = cmd.tab(inp);
            var last_idx = inp.length-1;
            var suggested = false;
            if (last_idx == 0) {
                suggested = get_suggestions(results, "");
            } else {
                suggested = get_suggestions(results, inp[last_idx]);
            }
            
            if (cmd.tab_final) {
                cmd.tab_final(suggested);
            }
            
            return;
        }
    }
    
    beep();
}

function command()
{
    var inp = input();
    if (inp.length == 0) {
        return;
    }
    
    if (inp[0] in _commands) {
        var cmd = _commands[inp[0]];
        cmd.invoke(inp);
    } else if (inp[0]) {
        print("No such command: '" + inp[0] + "'");
    }
}

function main_loop()
{
    while (true) {
        
        reset_line();
        write_string(_prompt);
        flush();
        
        // Read command
        while (_c != 0x0D) {
            _c = _reader.read();
            
            // End of line
            if (_line_idx == MAX_LENGTH) {
                if ((_c != 0x7F) && (_c != 0x08)) {
                    beep();
                    continue;
                }
            }
            
            switch(_c) {
            case 0x08:
                backspace();
                break;
            case 0x7F:
                backspace();
                break;
            case 0x09:
                tab();
                break;
            default:
                _line[_line_idx++] = _c;
                break;
            }
            
            render(_c == 0x0D);
        }
        
        // Issue command
        command();
    }
}

//////////
// MAIN //
//////////

var _command_names = new Array();

// Init commands
for (var cmd in _commands) {
    if (_commands[cmd].init) {
        _commands[cmd].init();
    }
    _command_names.push(cmd);
}

update_prompt();

// Main loop
main_loop();
