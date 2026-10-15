import axios from "axios";
import https from "node:https";
import { config } from "../config.js";

export interface HaState {
  entity_id: string;
  state: string;
  attributes: Record<string, unknown>;
  last_changed: string;
  last_updated: string;
}

const httpsAgent = new https.Agent({ ca: config.caCert, keepAlive: true });

export const haClient = axios.create({
  baseURL: `${config.baseUrl}/api`,
  headers: {
    Authorization: `Bearer ${config.apiToken}`,
    "Content-Type": "application/json",
  },
  httpsAgent,
  timeout: 10_000,
});

export async function getStates(): Promise<HaState[]> {
  const { data } = await haClient.get<HaState[]>("/states");
  return data;
}

export async function getState(entityId: string): Promise<HaState | null> {
  try {
    const { data } = await haClient.get<HaState>(`/states/${entityId}`);
    return data;
  } catch (err) {
    if (axios.isAxiosError(err) && err.response?.status === 404) {
      return null;
    }
    throw err;
  }
}

export async function callService(
  domain: string,
  service: string,
  body: Record<string, unknown>
): Promise<HaState[]> {
  const { data } = await haClient.post<HaState[]>(`/services/${domain}/${service}`, body);
  return data;
}
