var java_cmd = {
    invoke: function(argv) {
        argv.shift();  // Remove the command name ("java")
        Packages.RunJava.main(argv);
        return 0;
    },
};
