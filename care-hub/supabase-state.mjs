// Server-only storage for the small, single-instance Buddy prototype.
// The table is created by docs/SUPABASE_SETUP.sql. Never send this key to a browser.
export class SupabaseState {
  constructor({url, secretKey, fetchImpl = fetch}) {
    let parsed;
    try { parsed = new URL(url); } catch { throw new Error('Set SUPABASE_URL to the project HTTPS origin.'); }
    if (parsed.protocol !== 'https:' || parsed.username || parsed.password || parsed.pathname !== '/' || parsed.search || parsed.hash)
      throw new Error('SUPABASE_URL must be the project HTTPS origin, without a path or credentials.');
    if (!secretKey?.startsWith('sb_secret_'))
      throw new Error('Set SUPABASE_SECRET_KEY to a server-only sb_secret_ key.');
    this.endpoint = new URL('/rest/v1/buddy_private_state', parsed);
    this.secretKey = secretKey;
    this.fetchImpl = fetchImpl;
  }

  forKey(id) {
    if (!/^[a-z0-9-]+$/.test(id)) throw new Error('Invalid Buddy state identifier.');
    const query = new URL(this.endpoint);
    query.searchParams.set('id', `eq.${id}`);
    return {
      load: async () => {
        query.searchParams.set('select', 'value');
        const response = await this.request(query, {method:'GET'});
        const rows = await response.json();
        if (!Array.isArray(rows) || rows.length > 1) throw new Error('Supabase returned an invalid Buddy state response.');
        return rows.length ? rows[0].value : null;
      },
      save: async value => {
        const upsert = new URL(this.endpoint);
        upsert.searchParams.set('on_conflict', 'id');
        await this.request(upsert, {
          method:'POST',
          headers:{'content-type':'application/json',prefer:'resolution=merge-duplicates,return=minimal'},
          body:JSON.stringify({id,value})
        });
      }
    };
  }

  async request(url, options) {
    const response = await this.fetchImpl(url, {
      ...options,
      headers:{...options.headers,apikey:this.secretKey},
      signal:AbortSignal.timeout(15000)
    });
    if (!response.ok) {
      // Don't include response bodies: a proxy could echo private data or credentials.
      throw new Error(`Supabase storage unavailable (HTTP ${response.status}). Check the table, URL, and server secret key.`);
    }
    return response;
  }
}
