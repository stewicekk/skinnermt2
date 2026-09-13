import torch
from model import SimpleWeightNet
import argparse

def export_model(checkpoint, out):
    # load weights and export scripted model
    # This expects model architecture compatible with the checkpoint (in_ch=3, hidden configurable, out_ch inferred)
    state = torch.load(checkpoint, map_location='cpu')
    # infer out_ch from state dict if possible
    out_features = None
    for k in state.keys():
        if 'net.4.weight' in k or 'net.4.bias' in k:
            # net.4 is final linear weight, infer shape
            w = state[k]
            try:
                out_features = w.size(0)
            except:
                pass
    if out_features is None:
        out_features = 32
    model = SimpleWeightNet(in_ch=3, hidden=128, out_ch=out_features)
    model.load_state_dict(state)
    model.eval()
    # create example input
    example = torch.randn(1,3)
    traced = torch.jit.trace(model, example)
    traced.save(out)
    print(f"Exported TorchScript model to {out}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--checkpoint', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    export_model(args.checkpoint, args.out)
