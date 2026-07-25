import type {
  CommandId,
  ConfirmationPostingIds,
  PostingId,
  ReversalPostingIds,
} from "./contracts";
import { decodeCommandId, decodePostingId } from "./decode";

export function newCommandId(): CommandId {
  return decodeCommandId(crypto.randomUUID());
}

function postingId(scope: string, suffix: string): PostingId {
  return decodePostingId(`P-${scope}-${suffix}`);
}

export function newConfirmationPostingIds(): ConfirmationPostingIds {
  const scope = crypto.randomUUID();
  return {
    payControlDebit: postingId(scope, "PCD"),
    payPayableCredit: postingId(scope, "PPC"),
    receiveReceivableDebit: postingId(scope, "RRD"),
    receiveControlCredit: postingId(scope, "RCC"),
  };
}

export function newReversalPostingIds(): ReversalPostingIds {
  const scope = crypto.randomUUID();
  return {
    payControlCredit: postingId(scope, "PCC"),
    payPayableDebit: postingId(scope, "PPD"),
    receiveReceivableCredit: postingId(scope, "RRC"),
    receiveControlDebit: postingId(scope, "RCD"),
  };
}
