#ifndef MAR_LSP_H
#define MAR_LSP_H

/* Run the LSP JSON-RPC server on stdin/stdout.
   Returns when the client sends exit. */
int lsp_run(void);

#endif