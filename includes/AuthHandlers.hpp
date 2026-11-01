#ifndef AUTHHANDLERS_HPP
#define AUTHHANDLERS_HPP

class Client;
class SessionManager;

void handleLogin(Client &client, SessionManager &sessions);
void handleLogout(Client &client, SessionManager &sessions);

#endif
