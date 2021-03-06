
charset = ['0123456789', ...
	   'abcdefghijklmnopqrstuvwxyz', ...
	   'ABCDEFGHIJKLMNOPQRSTUVWXYZ'];


msg_size = CisInterface('CIS_MSG_BUF');
fprintf('maxMsgCli(M): Hello message size is %d.\n', msg_size);

% Create a max message, send/recv and verify
rpc = CisInterface('CisRpcClient', 'maxMsgSrv_maxMsgCli', '%s', '%s');

% Create a max message
output = randsample(charset, msg_size-1, true);

% Call RPC server
[flag, vars] = rpc.call(output);
if (~flag)
  error('maxMsgCli(M): RPC ERROR');
  exit(-1);
end;

% Check to see if response matches
if (vars{1} ~= output)
  error('maxMsgCli(M): ERROR: input/output do not match');
  exit(-1);
else
  disp('maxMsgCli(M): CONFIRM');
end;

% All done, say goodbye
disp('maxMsgCli(M): Goodbye!');
exit(0);
