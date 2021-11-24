// Header-only, modern C++ argument parser based on ARGP
//
// Copyright (C) 2018-2019 Patrick Boettcher <p@yai.se>
//
// SPDX-License-Identifier: LGPL-3.0
//
// Version: 1.0.0
//
// Project website: https://github.com/pboettch/cxx_argp
#ifndef CXX_ARGP_PARSER_H__
#define CXX_ARGP_PARSER_H__

#include <argp.h>

#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace cxx_argp
{
	using arg_parser = std::function<error_t(int key, const char *, struct argp_state *state)>;

	/* for floating-point types */
	template <typename T>
	typename std::enable_if<std::is_floating_point<T>::value, arg_parser>::type
	make_check_function(T &x)
	{
		return [&x](int, const char *arg, struct argp_state* state) {
			try {
				x = std::stod(arg);
				return 0;
			} catch(std::exception &err) {
				argp_error(
					state, "unable to interpret '%s' as a decimal, %s",
					arg, err.what());
				return -1;
			}
		};
	}

	/* for integers */
	template <typename T>
	typename std::enable_if<std::numeric_limits<T>::is_integer, arg_parser>::type
	make_check_function(T &x)
	{
		return [&x](int, const char *arg, struct argp_state* state) {
			try {
				x = std::stol(arg);
				return 0;
			} catch(std::exception &err) {
				argp_error(
					state, "unable to interpret '%s' as a whole number, %s",
					arg, err.what());
				return -1;
			}
		};
	}

	/* specialised for std::strings */
	inline arg_parser make_check_function(std::string &x)
	{
		return [&x](int, const char *arg, struct argp_state*) { x = arg; return 0; };
	}

	/* specialised for file-streams */
	inline arg_parser make_check_function(std::ifstream &x)
	{
		return [&x](int, const char *arg, struct argp_state* state) {
			x = std::ifstream{arg};
			if (x.good()) {
				return 0;
			}
			argp_error(state, "unable to open '%s'", arg);
			return -1;
		};
	}

	/* specialised for a pair of file-stream and its filename */
	template <typename T>
	inline arg_parser make_check_function(std::pair<T, std::string> &x)
	{
		return [&x](int, const char *arg, struct argp_state* state) {
			x = {T{arg}, arg};
			if (x.first.good()) {
				return 0;
			}
			argp_error(state, "unable to open '%s'", arg);
			return -1;
		};
	}

	/* specialise above for vectors */
	template<typename T>
	inline arg_parser make_check_function(std::vector<T> &x)
	{
		return [&x](int key, const char *arg, struct argp_state* state) {
			std::stringstream s{arg};
			std::string val;
			while (getline(s, val, ',')) {
				T val;
				if (make_check_function(val)(key, arg, state) != 0) {
					return -1;
				}
				x.push_back(val);
			}
			return 0;
		};
	}

	/* specialisation for bool -> set to true */
	inline arg_parser make_check_function(bool &x)
	{
		return [&x](int, const char *, struct argp_state*) { x = true; return 0; };
	}

class parser
{
	//< argp-option-vector
	std::vector<argp_option> options_ = {{}};

	//< conversion-function - from const char *arg to value
	std::map<int, arg_parser> convert_;

	//< expected positional argument count (-1, unlimited)
	ssize_t expected_argument_count_ = 0;

	//< positional arguments
	std::vector<std::string> arguments_;

	unsigned flags_ = 0;


	//! argp-callback
	static error_t parseoptions_cb_(int key, char *arg, struct argp_state *state)
	{
		return reinterpret_cast<cxx_argp::parser *>(state->input)
			->parseoptions_(key, arg, state);
	}

protected:
	virtual error_t parseoptions_(int key, char *arg, struct argp_state *state)
	{
		switch (key) {
		case ARGP_KEY_INIT:
			arguments_.clear();
			break;

		case ARGP_KEY_ARG:
			arguments_.push_back(arg);
			break;

		case ARGP_KEY_END:
			if (expected_argument_count_ == -1)
				break;

			if (arguments_.size() > (size_t) expected_argument_count_)
				argp_error(state, "too many arguments given");
			else if (arguments_.size() < (size_t) expected_argument_count_)
				argp_error(state, "too few arguments given");
			break;

		case ARGP_KEY_ERROR:
			break;

		default: {
			auto option = convert_.find(key);
			if (option != convert_.end()) {
				return option->second(key, arg, state);
			}
		}}

		return 0;
	}

public:
	bool help_via_argp_flags;

	parser(size_t expected_argument_count = 0) :
		expected_argument_count_(expected_argument_count),
		help_via_argp_flags{true} {}

	// add an argp-option to the options we care about
	template <typename T>
	void add_option(const argp_option &option, T &var)
	{
		add_option(option, make_check_function(var));
	}

	void add_option(const argp_option &option,
	                const arg_parser &&custom)
	{
		options_.insert(options_.end() - 1, option);
		convert_.insert({option.key, custom});
	}

	void add_option(const argp_option &option,
	                const std::function<bool(const char *)> &&custom)
	{
		options_.insert(options_.end() - 1, option);
		convert_.insert({option.key, [custom](int key, const char *arg, struct argp_state* state) {
			if (!custom(arg)) {
				argp_error(state, "argument '%s' not usable for '%c'", arg, key);
				return -1;
			}
			return 0;
		}});
	}

	// processes arguments with argp_parse and evaluate standarda arguments
	bool parse(int argc, char *argv[], const char *usage = "", const char *doc = nullptr)
	{
		struct argp argp = {options_.data(), parser::parseoptions_cb_, usage, doc};

		int ret = argp_parse(&argp, argc, argv, flags_, nullptr, this);

		if (flags_ & ARGP_NO_ERRS)
			return true;

		const bool help_disabled = help_via_argp_flags && (flags_ & ARGP_NO_HELP);

		if (ret != 0) {
			if (!help_disabled)
				argp_help(&argp, stderr, ARGP_HELP_USAGE, argv[0]);
			return false;
		}

		if (expected_argument_count_ != -1 &&
		    (size_t) expected_argument_count_ != arguments_.size()) {
			if (!help_disabled)
				argp_help(&argp, stderr, ARGP_HELP_USAGE, argv[0]);
			return false;
		}

		return true;
	}

	void add_flags(unsigned flags) { flags_ |= flags; }
	void remove_flags(unsigned flags) { flags_ &= ~flags; }

	const std::vector<std::string> &arguments() const { return arguments_; }
};
} // namespace cxx_argp

#endif // CXX_ARGP_PARSER_H__
