import Fastify, { type FastifyError } from "fastify";
import { config } from "./config.js";
import { switchRoutes } from "./routes/switches.js";

async function main(): Promise<void> {
  const app = Fastify({
    logger: true,
  });

  app.get("/health", async () => ({ status: "ok" }));

  await app.register(switchRoutes, { prefix: "/" });

  app.setErrorHandler((err: FastifyError, req, reply) => {
    const status = typeof err.statusCode === "number" ? err.statusCode : 502;
    app.log.error(err);
    reply
      .code(status)
      .send({ error: status < 500 ? err.message : "Home Assistant request failed" });
  });

  await app.listen({ port: config.port, host: "0.0.0.0" });
}

for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.on(signal, () => {
    process.exit(0);
  });
}

main().catch((err) => {
  console.error("Fatal startup error:", err.message);
  process.exit(1);
});
