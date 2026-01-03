#pragma once
#include <vector>
#include "router.hpp"
#include "middleware.hpp"

class App {
public:
  App(Router router, std::vector<Middleware> mws);

  void handle(ReqContext& ctx, const Request& req, Response& res);

private:
  Router router_;
  std::vector<Middleware> mws_;
};