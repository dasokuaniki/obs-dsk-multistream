# DSK Comment Viewer integration

DSK Multistream and DSK Comment Viewer are installed, updated, licensed, and removed independently. Multistream is usable without Comment Viewer and does not bundle or manage Viewer files, settings, accounts, or credentials.

At OBS startup, the integration is automatically on when a complete Viewer installation is present in `%LOCALAPPDATA%\DSKCommentViewer`; otherwise it starts off. The **Comment Viewer integration** check in the `DSK Streaming` gear menu controls the integration for the current OBS session. When enabled, Multistream first requests:

```http
GET http://127.0.0.1:17321/api/integrations/obs/v1
Accept: application/json
```

If the first request fails, Multistream starts the independent Viewer server and retries the same endpoint. An already-running Viewer is not started again. The validated v1 response identifies `dsk-comment-viewer`, declares the `obs-browser-dock` integration, and exposes the fixed path `/viewer?dock=chat&send=1`. Multistream constructs and accepts only `http://127.0.0.1:17321/viewer?dock=chat&send=1`; it never follows a dock host or arbitrary URL supplied by the response.

The integration endpoint contains no tokens, account details, comment content, or stream keys. Comment sending remains inside the Viewer and is protected by Viewer's loopback and request-origin checks.

After validation, Multistream creates the plugin-owned `DSK Comments` browser dock with ID `dskcommentsviewer`. If Viewer is absent, incompatible, or unhealthy, no dead dock is created and all streaming features remain available. The optional **Open DSK Comment Viewer** Tools item appears when the independent Viewer installation is detected.

Turning **Comment Viewer integration** off cancels pending checks and removes only the `dskcommentsviewer` dock for the current OBS session. It does not stop Viewer or remove Viewer files, settings, accounts, or credentials. If Viewer remains installed, the integration starts on again at the next OBS startup.

The full cross-project contract and compatibility rules are maintained in the Viewer repository as `DSK_MULTISTREAM_INTEGRATION_HANDOFF.md`. Breaking changes require a versioned v2 endpoint instead of silently changing v1.
