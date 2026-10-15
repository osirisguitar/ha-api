import type { FastifyInstance } from "fastify";
import { getStates, getState, callService, type HaState } from "../ha/client.js";

function entityId(name: string): string {
  return `switch.${name}`;
}

export interface SwitchView {
  name: string;
  entity_id: string;
  state: string;
  friendly_name: unknown;
  attributes: Record<string, unknown>;
  last_changed: string;
  last_updated: string;
}

type SwitchResponse = SwitchView | { error: string };

function toSwitchView(state: HaState): SwitchView {
  return {
    name: state.entity_id.replace(/^switch\./, ""),
    entity_id: state.entity_id,
    state: state.state,
    friendly_name: state.attributes.friendly_name,
    attributes: state.attributes,
    last_changed: state.last_changed,
    last_updated: state.last_updated,
  };
}

export async function switchRoutes(app: FastifyInstance): Promise<void> {
  app.get<{ Reply: SwitchView[] }>("/switches", async () => {
    const states = await getStates();
    return states.filter((s) => s.entity_id.startsWith("switch.")).map(toSwitchView);
  });

  app.get<{ Params: { name: string }; Reply: SwitchResponse }>(
    "/switch/:name",
    async (req, reply) => {
      const state = await getState(entityId(req.params.name));
      if (!state) {
        return reply.code(404).send({ error: `Switch "${req.params.name}" not found` });
      }
      return toSwitchView(state);
    }
  );

  app.post<{ Params: { name: string }; Reply: SwitchResponse }>(
    "/switch/:name/on",
    async (req, reply) => {
      const id = entityId(req.params.name);
      const before = await getState(id);
      if (!before) {
        return reply.code(404).send({ error: `Switch "${req.params.name}" not found` });
      }
      await callService("switch", "turn_on", { entity_id: id });
      const after = await getState(id);
      return toSwitchView(after ?? before);
    }
  );

  app.post<{ Params: { name: string }; Reply: SwitchResponse }>(
    "/switch/:name/off",
    async (req, reply) => {
      const id = entityId(req.params.name);
      const before = await getState(id);
      if (!before) {
        return reply.code(404).send({ error: `Switch "${req.params.name}" not found` });
      }
      await callService("switch", "turn_off", { entity_id: id });
      const after = await getState(id);
      return toSwitchView(after ?? before);
    }
  );
}
