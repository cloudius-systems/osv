// Pretty print suggestions
function write_suggestions(arr, command)
{
    write_char('\n');
    for (var i=0; i < arr.length; i++) {
        var suggestion = arr[i];
        if ((command != undefined) && (command.tab_pretty)) {
            suggestion = command.tab_pretty(suggestion);
        }
        write_string(suggestion);
        
        if (i != arr.length-1) {
            var ch = '\t';
            if ((command != undefined) && (command.tab_delim)) {
                ch = command.tab_delim();
            }
            write_char(ch);
        }
    }
    write_char('\n');
    flush();
}

//
// Finds last word in line and replace it with the one in the argument
// FIXME: Take MAX_LENGTH into account
//
function autocomplete(word)
{
    word = String(word);
    
    for (var i=_line_idx; i>=0; i--) {
        if (i == 0) {
            _line_idx = 0;
        } else {
            if (_line[i] != 0x20) {
                _line_idx = i;
                continue;
            }
        }
        
        // Copy new word to buffer in place of the old one
        for (var j=0; j<word.length; j++) {
            _line[_line_idx++] = ord(word[j]);
        }
        
        _line[_line_idx++] = 0x20;
        return;
    }
}

function get_suggestions(arr, partial, command)
{
    // Find suggestions
    var suggestions = new Array();
    for (var cmd in arr) {
        if ((arr[cmd]).search(partial) == 0) {
            suggestions.push(arr[cmd]);
        }
    }
    
    // Found only 1 suggestion
    if (suggestions.length == 1) {
        autocomplete(suggestions[0]);
        return (true);
    } else if (suggestions.length > 1) {
        // FIXME: Find a common prefix for all suggestions if possible
        write_suggestions(suggestions, command);
    } else {
        beep();
    }
    
    return (false);
}
