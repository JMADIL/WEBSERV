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
       srcs/http/HttpRequest.cpp \
       srcs/http/HttpResponse.cpp \
       srcs/http/RequestParser.cpp \
       srcs/http/ResponseBuilder.cpp \
       srcs/utils/MimeTypes.cpp \
       srcs/cgi/CgiHandler.cpp \
       srcs/cgi/CgiBridge.cpp \
       srcs/cgi/CgiLauncher.cpp \
       srcs/cgi/CgiResponse.cpp \
       srcs/sessions/Session.cpp \
       srcs/sessions/SessionManager.cpp \
       srcs/sessions/AuthHandlers.cpp
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
	python3 tests/test_webserv.py

-include $(DEPS)
