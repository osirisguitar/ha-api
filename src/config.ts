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

const caCertPath = resolve(process.env.HA_CA_CERT_PATH ?? "./certs/home-ca.crt");

export const config = {
  baseUrl: (process.env.HA_BASE_URL ?? "https://home.int.bornholm.se").replace(/\/+$/, ""),
  apiToken: required("HA_API_TOKEN"),
  caCertPath,
  caCert: readFileSync(caCertPath),
  port: Number(process.env.PORT ?? 3000),
};
