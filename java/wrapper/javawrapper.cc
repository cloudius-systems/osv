/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _TEST_ONLY_
#include <osv/elf.hh>
#endif

#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <map>

inline bool is_whitespace(char c) {
	if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n')) {
		return true;
	} else {
		return false;
	}
}

inline bool starts_with(const char *s, const char *prefix) {
    return (strncmp(s, prefix, strlen(prefix)) == 0);
}

static const char *find_key(std::map<std::string,std::string> &configMap, std::string& key) {
	std::map<std::string,std::string>::iterator	it=configMap.find(key);
	if (it == configMap.end()) {
		return NULL;
	}
	
	std::string	value=it->second;
	return value.c_str();
}

inline const char *find_key(std::map<std::string,std::string> &configMap, const char *key) {
	std::string	str(key);
	return find_key(configMap, str);
}

static char *trim(char *s) {
	size_t startPos=0, maxPos=strlen(s), endPos=maxPos;
	for ( ; startPos < endPos; startPos++) {
		char c=s[startPos];
		if (!is_whitespace(c)) {
			break;
		}
	}
	
	for ( ; endPos > startPos; endPos--) {
		char c=s[endPos - 1];
		if (!is_whitespace(c)) {
			break;
		}
	}
	
	if ((startPos == 0) && (endPos == maxPos)) {
		return s;
	}

	// check if trimmed any characters at end of string
	if (endPos < maxPos) {
		s[endPos] = '\0';
	}

	// check if trimmed any characters at start of string
	if (startPos > 0) {
		size_t curPos=0;
		for ( ; startPos < endPos; startPos++, curPos++) {
			s[curPos] = s[startPos];
		}
		s[curPos] = '\0';
	}
	
	return s;
}

static int get_values_list(std::map<std::string,std::string> &configMap, const char *prefix, std::vector<std::string> &out) {
	int index=1;

	for ( ; ; index++) {
		std::string	key(prefix);
		key += std::string(".");
		key += std::to_string(index);
		
		const char	*value=find_key(configMap, key);
		if (value == NULL) {
			break;
		}
		
		out.push_back(std::string(value));
	}

	return index - 1;
}

static char *duplicate_wrapper_value(const std::string& str) {
	return strdup(str.c_str());
}

static int duplicate_wrapper_values(std::vector<std::string>& values, char **argv, int startPos, int numValues) {
	int	curPos=startPos, index=0;
	for (index=0; index < numValues; index++, curPos++) {
		argv[curPos] = duplicate_wrapper_value(values[index]);
	}

	return curPos;
}

static int process_wrapper_configuration(const char *argv0, std::map<std::string,std::string> &configMap, int& argc, char **&argv) {
	const char	*mainClassName=find_key(configMap, "wrapper.java.mainclass");
	if (mainClassName == NULL) {
		std::cerr << "Missing main class specification in wrapper configuration.\n";
		return 1;
	}

	std::vector<std::string>	jvmDefs, appParams, classpath;
	int							numJvmDefs=get_values_list(configMap, "wrapper.java.additional", jvmDefs);
	int							numClasspath=get_values_list(configMap, "wrapper.java.classpath", classpath);
	int							numAppParams=get_values_list(configMap, "wrapper.app.parameter", appParams);
	int							totalParams=1 /* argv[0] */ + ((numClasspath > 0) ? 2 /* -cp + path */ : 0) + numJvmDefs + 1 /* main class */ + numAppParams + 1 /* last is NULL */;
	char						**replArgv=(char **) malloc(totalParams * sizeof(char *));
	replArgv[0] = strdup(argv0);	// argv[0] is the same as the original
	
	int	curPos=duplicate_wrapper_values(jvmDefs, replArgv, 1, numJvmDefs);
	if (numClasspath > 0) {
		std::string	joined;
		for (int index=0; index < numClasspath; index++) {
			std::string	path=classpath[index];
			if (joined.length() > 0) {
				joined += std::string(":");	// NOTE: we use the Linux path separator as OSV is Linux-like
			}
			
			joined += path;
		}

		replArgv[curPos] = strdup("-cp");
		curPos++;
		replArgv[curPos] = duplicate_wrapper_value(joined);
		curPos++;
	}

	replArgv[curPos] = strdup(mainClassName);
	curPos++;
	curPos = duplicate_wrapper_values(appParams, replArgv, curPos, numAppParams);
	replArgv[curPos] = NULL;	// mark end
	
	argc = curPos;
	argv = replArgv;
	return 0;
}

// TODO use a verbosity level instead of a bool
static int read_wrapper_configuration(const char *path, std::map<std::string,std::string> &configMap, bool verbose);

#define MAX_WRAPPER_LINE_LENGTH	1024
static int read_wrapper_configuration_file(FILE *fp, std::map<std::string,std::string> &configMap, bool verbose) {
	char	line[MAX_WRAPPER_LINE_LENGTH + sizeof(int)];
	for (int lineNumber=1; fgets(line, MAX_WRAPPER_LINE_LENGTH, fp) != NULL; lineNumber++) {
		line[MAX_WRAPPER_LINE_LENGTH] = '\0';	// just make sure
		trim(line);
		
		// skip empty and/or comment lines
		if ((line[0] == '\0') || (line[0] == '#')) {
			continue;
		}
		
		char	*sep=NULL;
		if (starts_with(line, "@include")) {
			if ((sep=strchr(line, ' ')) == NULL) {
				std::cerr << "Missing included file path at line #" << lineNumber  << " (" << line << ") of wrapper configuration.\n";
				return 1;
			}
			
			sep = trim(sep);
			if (*sep == '\0') {
				std::cerr << "Empty included file path at line #" << lineNumber  << " (" << line << ") of wrapper configuration.\n";
				return 1;
			}
			
			int	err=read_wrapper_configuration(sep, configMap, verbose);
			if (err != 0) {
				return err;
			}
		} else {
			if ((sep=strchr(line, '=')) == NULL) {
				std::cerr << "Missing name/value separator at line #" << lineNumber  << " (" << line << ") of wrapper configuration.\n";
				return 1;
			}
			
			*sep = '\0';	// create 2 strings
			
			std::string	name(line), value(trim(sep + 1));
			const char	*prev=find_key(configMap, name);
			if (prev != NULL) {
				std::cerr << "Multiple values for " << name.c_str() << "\n";
				return 1;
			}
			
			configMap[name] = value;
		}
	}
	
	return 0;
}

static int read_wrapper_configuration(const char *configFilePath, std::map<std::string,std::string> &configMap, bool verbose) {
	FILE	*fp=fopen(configFilePath, "r");
	if (fp == NULL) {
        std::cerr << "Can't open " << configFilePath  << " to read wrapper configuration.\n";
		return 1;
	}

	if (verbose) {
		std::cout << "Load configuration from " << configFilePath << "\n";
	}

	int	err=read_wrapper_configuration_file(fp, configMap, verbose);
	fclose(fp);
	return err;
}

extern "C"
int main(int argc, char **argv)
{
	const char	*javasopath="/java.so";
	bool		verbose=false, dryRun=false;
	int			confIndex=1;

	for ( ; confIndex < argc; confIndex++) {
		const char	*argValue=argv[confIndex];
		if (strcmp(argValue, "-verbose") == 0) {
			verbose = true;
		} else if (strcmp(argValue, "-dry-run") == 0) {
			dryRun = true;
		} else if (strcmp(argValue, "-javasopath") == 0) {
			confIndex++;
			if (confIndex >= argc) {
				std::cerr << "Missing java.so path argument\n";
				return 1;
			}
			javasopath = argv[confIndex];
		} else {
			break;
		}
	}

	if (confIndex >= argc) {
		std::cerr << "Missing wrapper configuration file argument at index=" << confIndex << "\n";
		return 1;
	}

	std::map<std::string,std::string>	configMap;
	const char							*configFilePath=argv[confIndex];
	int 								err=read_wrapper_configuration(configFilePath, configMap, verbose);
	if (err != 0) {
		return err;
	}

	if	(verbose) {
		std::cout << "Process configuration from " << configFilePath << "\n";
	}

	int		jvmArgc=0;
	char	**jvmArgv=NULL;
	if ((err=process_wrapper_configuration(javasopath, configMap, jvmArgc, jvmArgv)) != 0) {
		return err;
	}

	if (verbose) {
		for (int index=0; index < jvmArgc; index++) {
			const char	*v=jvmArgv[index];
			std::cout << "\targv[" << index << "]: " << v << '\n';
		}
	}

#ifndef _TEST_ONLY_
    auto prog=elf::get_program();
	if	(verbose) {
		std::cout << "Load library " << javasopath << "\n";
	}

	auto javaso=prog->get_library(javasopath);
	if (!javaso) {
		std::cerr << "Failed to load " << javasopath << "\n";
		return 1;
	}
#endif	/* _TEST_ONLY_ */

	if	(verbose) {
		std::cout << "Look for 'main' in " << javasopath << "\n";
	}

#ifndef _TEST_ONLY_
	auto main=javaso->lookup<int (int, char**)>("main");
	if (!main) {
		std::cerr << "No main() in " << javasopath << "\n";
		return 1;
	}
#endif	/* _TEST_ONLY_ */

	if (dryRun) {
		std::cout << "Skip invocation of 'main' in " << javasopath << "\n";
	} else {
		if (verbose) {
			std::cout << "Invoke 'main' of " << javasopath << "\n";
		}

#ifndef _TEST_ONLY_
		if ((err=main(jvmArgc, jvmArgv)) != 0) {
			return err;
		}
#endif	/* _TEST_ONLY_ */
	}

	return 0;
}