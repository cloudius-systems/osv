package io.osv.isolated;

import io.osv.util.ClassDiagnostics;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static io.osv.util.ClassDiagnostics.JAVA_DIAGNOSTICS_PROPERTY_NAME;

/**
 * MultiJarLoader load multiple jars with their main and command line when using
 * this class as the main class, it uses a configuration file to load multiple
 * jar it's currently using a text file with multiple lines, each starts with
 * the jar file, followed by their command lines.
 *
 * @author amnon
 */
public class MultiJarLoader {
    /**
     * the main file runs the loaded file.
     *
     * @param args - it looks for a javamains file, with the -mains flag e.g.
     *             - mains /usr/mgmt/myfile.txt
     */
    public static void main(String[] args) {
        final boolean showDiagnostics = ClassDiagnostics.showDiagnostics(args);
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("-mains")) {
                if (i + 1 >= args.length) {
                    System.err.println("No file specified for load from file");
                }
                runFromFile(args[i + 1],showDiagnostics);
                return;
            }
        }
        System.err
                .println("No load file was specified, using default /usr/mgmt/javamains, use -mains to overide");
        runFromFile("/usr/mgmt/javamains",showDiagnostics);
    }

    /**
     * runFromFile read a java mains file and execute each of the commands
     * each line with its own main, will be run on its own thread
     *
     * @param fileName the file name to read from
     */
    private static void runFromFile(String fileName,boolean showDiagnostics) {
        FileReader fr;
        try {
            File f = new File(fileName);
            fr = new FileReader(f);
        } catch (Exception e) {
            System.err.println("failed opening " + fileName
                    + " with exception " + e);
            return;
        }

        try(final BufferedReader reader = new BufferedReader(fr)) {
            String line;
            while ((line = reader.readLine()) != null) {
                String trimmedLine = line.trim();
                if (isExec(trimmedLine)) {
                    final RunOnThread thread = new RunOnThread(trimmedLine,showDiagnostics);
                    thread.start();
                }
            }
        } catch (IOException e) {
            System.err.println("failed reading from " + fileName
                    + " with exception " + e);
            e.printStackTrace();
        }
    }

    /**
     * when reading the java mains file, check if a line is an executable line.
     *
     * @param line a line from the file
     * @return true if this is an executable line, comments and empty lines are
     * ignored. sleep with a number in millisecond is supported, and
     * would put the main thread into sleep
     */
    private static boolean isExec(String line) {
        if (line.equals("") || line.startsWith("#")) {
            return false;
        }
        if (line.startsWith("sleep ")) {
            try {
                Thread.sleep(Long.valueOf(line.substring(6)));
            } catch (NumberFormatException | InterruptedException e) {
                System.err.println("failed sleeping " + line
                        + " with exception " + e);
                e.printStackTrace();
            }
            return false;
        }
        return true;
    }

    /**
     * A class to run each of the java command on its own thread. Note that an
     * exception would terminate the thread, but will be caught and will not
     * terminate the process
     *
     * @author amnon
     */
    private static class RunOnThread extends Thread {
        private String args;
        private boolean showDiagnostics;

        RunOnThread(String args,boolean showDiagnostics) {
            this.args = args;
            this.showDiagnostics = showDiagnostics;
        }

        @Override
        public void run() {
            try {
                final List<String> argumentsList = new ArrayList<>();
                if(showDiagnostics) {
                    argumentsList.add("-D" + JAVA_DIAGNOSTICS_PROPERTY_NAME);
                }
                argumentsList.addAll(Arrays.asList(args.split("\\s+")));

                final String[] argumentsArray = argumentsList.toArray(new String[] {});
                IsolatedJvm.getInstance().runSync(argumentsArray);
            } catch (Throwable e) {
                System.err.println("Exception was caught while running " + args
                        + " exception: " + e);
                e.printStackTrace();
            }
        }
    }
}
