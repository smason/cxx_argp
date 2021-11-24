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

#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace cxx_argp
{
	/* for floating-point types */
	template <typename T>
	typename std::enable_if<
	    std::is_floating_point<T>::value,
	    std::function<bool(const char *)>>::type
	make_check_function(T &x)
	{
		return [&x](const char *arg) {
			char *end;
			errno = 0;
			const double v = strtod(arg, &end);
			if (errno == 0 && *end == '\0') {
				x = v;
				return true;
			}
			return false;
		};
	}

	/* for integers */
	template <typename T>
	typename std::enable_if<
	    std::numeric_limits<T>::is_integer,
	    std::function<bool(const char *)>>::type
	make_check_function(T &x)
	{
		return [&x](const char *arg) {
			char *end;
			errno = 0;
			const long v = strtol(arg, &end, 0);
			if (errno == 0 && *end == '\0') {
				x = v;
				return true;
			}
			return false;
		};
	}

	/* specialised for std::strings */
	inline std::function<bool(const char *)> make_check_function(std::string &x)
	{
		return [&x](const char *arg) { x = arg; return true; };
	}

	/* specialised for file-streams */
	inline std::function<bool(const char *)> make_check_function(std::ifstream &x)
	{
		return [&x](const char *arg) {
			x = std::ifstream{arg};
			return x.good();
		};
	}

	/* specialised for a pair of file-stream and its filename */
	template<typename T>
	std::function<bool(const char *)> make_check_function(std::pair<T, std::string> &x)
	{
		return [&x](const char *arg) {
			x = {T{arg}, arg};
			return x.first.good();
		};
	}

	/* specialised for vectors with string */
	inline std::function<bool(const char *)> make_check_function(std::vector<std::string> &x)
	{
		return [&x](const char *arg) {
			std::stringstream s;
			s.str(arg);
			std::string val;

			while (getline(s, val, ','))
				x.push_back(val);

			return true;
		};
	}

	/* specialised for vectors with integers */
	template <typename T,
	          typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
	std::function<bool(const char *)> make_check_function(std::vector<T> &x)
	{
		return [&x](const char *arg) {
			std::stringstream s;
			s.str(arg);
			std::string val;

			while (getline(s, val, ',')) {
				try {
					x.push_back(std::stoi(val));
				} catch (std::exception &e) {
					return false;
				}
			}

			return true;
		};
	}

	/* specialisation for bool -> set to true */
	inline std::function<bool(const char *)> make_check_function(bool &x)
	{
		return [&x](const char *) { x = true; return true; };
	}

class parser
{
	std::vector<argp_option> options_ = {{}};                 //< argp-option-vector
	std::map<int, std::function<bool(const char *)>> convert_; //< conversion-function - from const char *arg to value
	ssize_t expected_argument_count_ = 0;                      //< expected positional argument count (-1, unlimited)
	std::vector<std::string> arguments_;                       //< positional arguments
	std::map<int, size_t> counter_;
	unsigned flags_ = 0;


	//! argp-callback
	static error_t parseoptions_(int key, char *arg, struct argp_state *state)
	{
		auto *parser = reinterpret_cast<cxx_argp::parser *>(state->input);

		auto option = parser->convert_.find(key);

		if (option != parser->convert_.end()) {
			parser->counter_[key]++;
			if (!option->second(arg)) {
				argp_error(state, "argument '%s' not usable for '%c'", arg, key);
				return -1;
			}
		} else { /* argp-argument handler */
			switch (key) {
			case ARGP_KEY_INIT:
				parser->arguments_.clear();
				break;

			case ARGP_KEY_ARG:
				parser->arguments_.push_back(arg);
				break;

			case ARGP_KEY_END:
				if (parser->expected_argument_count_ == -1)
					break;

				if (parser->arguments_.size() > (size_t) parser->expected_argument_count_)
					argp_failure(state, 1, 0, "too many arguments given");
				else if (parser->arguments_.size() < (size_t) parser->expected_argument_count_)
					argp_failure(state, 1, 0, "too few arguments given");
				break;

			case ARGP_KEY_ERROR:
				break;

			default:
				break;
			}
		}

		return 0;
	}

public:
	parser(size_t expected_argument_count_ = 0)
	    : expected_argument_count_(expected_argument_count_) {}

	// add an argp-option to the options we care about
	template <typename T>
	void add_option(const argp_option &o, T &var)
	{
		add_option(o, make_check_function(var));
	}

	void add_option(const argp_option &o,
	                const std::function<bool(const char *)> &&custom)
	{
		options_.insert(options_.end() - 1, o);
		convert_.insert(std::make_pair(o.key, custom));
		counter_.insert(std::make_pair(o.key, 0));
	}

	// processes arguments with argp_parse and evaluate standarda arguments
	bool parse(int argc, char *argv[], const char *usage = "", const char *doc = nullptr)
	{
		struct argp argp = {options_.data(), parser::parseoptions_, usage, doc};

		int ret = argp_parse(&argp, argc, argv, flags_, nullptr, this);

		if (flags_ & ARGP_NO_ERRS)
			return true;

		if (ret != 0) {
			if (!(flags_ & ARGP_NO_HELP))
				argp_help(&argp, stderr, ARGP_HELP_USAGE, argv[0]);
			return false;
		}

		if (expected_argument_count_ != -1 &&
		    (size_t) expected_argument_count_ != arguments_.size()) {
			if (!(flags_ & ARGP_NO_HELP))
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
