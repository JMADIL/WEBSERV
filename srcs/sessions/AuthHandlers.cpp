#include "AuthHandlers.hpp"
#include "Client.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Session.hpp"
#include "SessionManager.hpp"

#include <string>

void handleLogin(Client &client, SessionManager &sessions)
{
    const HttpRequest &req = client.getRequest();
    HttpResponse &res = client.getResponse();
    res.reset();
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Cache-Control", "no-store");

    std::string body(req.body.begin(), req.body.end());
    if (req.getHeader("content-type") != "application/x-www-form-urlencoded"
        || body != "user=demo&password=demo-only")
    {
        res.setStatus(401);
        res.setBody("Invalid login\n");
        return;
    }

    std::string oldId = req.getCookie("session_id");
    if (!oldId.empty())
        sessions.destroySession(oldId);

    Session *session = sessions.createSession();
    session->set("user", "demo");

    res.setStatus(200);
    res.setCookie("session_id", session->id, 1800, "/", true);
    res.setBody("Logged in\n");
}

void handleLogout(Client &client, SessionManager &sessions)
{
    const HttpRequest &req = client.getRequest();
    HttpResponse &res = client.getResponse();
    res.reset();

    std::string id = req.getCookie("session_id");
    if (!id.empty())
        sessions.destroySession(id);

    res.setStatus(200);
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Cache-Control", "no-store");
    res.setCookie("session_id", "", 0, "/", true);
    res.setBody("Logged out\n");
}