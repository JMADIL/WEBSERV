#include "ResponseBuilder.hpp"
#include "Client.hpp"
#include "Config.hpp"
#include "ServerBlock.hpp"
#include "LocationBlock.hpp"
#include "MimeTypes.hpp"
#include "CgiHandler.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>

