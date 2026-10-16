import "dotenv/config";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

function required(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing required env var: ${name}`);
  }
  return value;
}

// Home Assistant's cert is publicly trusted (Let's Encrypt via DNS-01, see
// kubernetes-operations/dns), so no custom CA is needed anymore. HA_CA_CERT_PATH
// stays as an escape hatch for pinning a CA, but is optional now.
const caCert = process.env.HA_CA_CERT_PATH
  ? readFileSync(resolve(process.env.HA_CA_CERT_PATH))
  : undefined;

export const config = {
  baseUrl: (process.env.HA_BASE_URL ?? "https://home.int.bornholm.se").replace(/\/+$/, ""),
  apiToken: required("HA_API_TOKEN"),
  caCert,
  port: Number(process.env.PORT ?? 3000),
};
