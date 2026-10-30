NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
CPPFLAGS = -Iincludes

SRCS = srcs/main.cpp \
       srcs/config/Config.cpp \
       srcs/config/ServerBlock.cpp \
       srcs/config/LocationBlock.cpp \
       srcs/core/Server.cpp \
       srcs/core/Client.cpp
OBJS = $(SRCS:%.cpp=build/%.o)
DEPS = $(OBJS:.o=.d)

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all clean fclean re test

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

test: $(NAME)
	python3 tests/run_config_tests.py
	python3 tests/test_startup.py
	python3 tests/run_core_tests.py

-include $(DEPS)
