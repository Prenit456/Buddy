-- Run once in the Supabase SQL Editor for the Buddy prototype.
-- The backend, not the browser or ESP32, accesses this table.
create table if not exists public.buddy_private_state (
  id text primary key,
  value jsonb not null
);

alter table public.buddy_private_state enable row level security;
revoke all on table public.buddy_private_state from anon, authenticated;
grant select, insert, update on table public.buddy_private_state to service_role;

-- Intentionally no anon/authenticated RLS policies: only the server secret key
-- may access account, pairing, and provider-priority state.
