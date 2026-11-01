NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
CPPFLAGS = -Iincludes

SRCS = srcs/main.cpp \
       srcs/config/Config.cpp \
       srcs/config/ServerBlock.cpp \
       srcs/config/LocationBlock.cpp \
       srcs/core/Server.cpp \
       srcs/core/Client.cpp \
       srcs/core/RequestParserAdapter.cpp \
       srcs/http/HttpRequest.cpp \
       srcs/http/HttpResponse.cpp \
       srcs/http/RequestParser.cpp \
       srcs/http/ResponseBuilder.cpp \
       srcs/utils/MimeTypes.cpp \
       srcs/cgi/CgiHandler.cpp \
       srcs/cgi/CgiBridge.cpp \
       srcs/cgi/CgiLauncher.cpp \
       srcs/cgi/CgiResponse.cpp
OBJS = $(SRCS:%.cpp=build/%.o)
DEPS = $(OBJS:.o=.d)

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all clean fclean re test test-owned test-http test-cgi-routing test-cgi-launch test-cgi-response test-cgi-core test-config-compat test-cgi-e2e

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

build/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf build

fclean: clean
	rm -f $(NAME)

re:
	$(MAKE) fclean
	$(MAKE) all

test: test-owned test-http

test-owned: $(NAME)
	python3 tests/run_config_tests.py
	python3 tests/test_config_compat.py
	python3 tests/run_cgi_routing_tests.py
	python3 tests/test_cgi_launch.py
	python3 tests/test_cgi_response.py
	python3 tests/test_startup.py
	python3 tests/run_core_tests.py
	python3 tests/run_http_parser_tests.py
	python3 tests/test_cgi_core.py
	python3 tests/test_resources.py
	python3 tests/test_cgi_e2e.py
	python3 tests/test_http.py
	python3 tests/test_examples.py

# The full HTTP acceptance checks remain red until the HTTP owner fixes the listed failures.
test-http: $(NAME)
	python3 tests/test_http_pending.py

test-cgi-e2e: $(NAME)
	python3 tests/test_cgi_e2e.py

# Resolver checks can run without starting a server.
test-cgi-routing:
	python3 tests/run_cgi_routing_tests.py

test-cgi-launch:
	python3 tests/test_cgi_launch.py

test-cgi-response:
	python3 tests/test_cgi_response.py

test-cgi-core:
	python3 tests/test_cgi_core.py

test-config-compat:
	python3 tests/test_config_compat.py

-include $(DEPS)
