## What is Sentinel?
💻 Sentinel is a high-performance semantic cache proxy built for LLM-powered applications.
It reduces latency and cost by intelligently caching and reusing responses for semantically similar queries, eliminating redundant calls to external LLM providers.

🧠 Unlike traditional caching systems (e.g. exact-match in Redis or Memcached), __Sentinel__ understands intent.
By transforming user prompts into vector embeddings and performing real-time similarity search, __Sentinel__ can detect queries that are different in wording but identical in meaning — and serve cached responses in under 50ms.

🔌 As a drop-in proxy layer, __Sentinel__ sits between users and LLM providers.
It handles request interception, on-premise vectorization via embedded inference models, and configurable semantic matching, ensuring both flexibility and control over caching behavior.

🚀 Sentinel is built for teams scaling LLM applications who care about performance, cost efficiency, and system-level control.

## Documentations
For a brief view of architectural decisions made, check out: [Design Doc](https://www.notion.so/Design-Doc-33e2628ce23c8039a18ed213d7bdcfdb?source=copy_link)
