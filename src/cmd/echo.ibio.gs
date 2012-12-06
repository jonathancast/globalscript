#! /home/jcast/globalscript/bin/ibio

for (
    'opt.n ← env.option ch{n};
    'as ← env.args.get;
) send $ concat (intersperse qq{ } as) <> analyze opt.n
    case true. qq{}
    case false. qq{\n}
(where
    'concat = foldr (<>) [];
    'intersperse = λ 'x. tail (>>= λ 'y. [ x, y, ]);
    ('<>) = λ 'xn 'ys. foldr (:) ys xn;
    'foldr = λ 'f 'z 'xn. analyze xn
        case nil. z
        case 'x:'xn1. f x (foldr f z xn1)
    ;
    'tail = λ 'f 'xn. analyze xn
        case nil. nil
        case 'x:'xn1. x : f xn1
    ;
    instance monad.c list = monad.asCategoryTheory 〈
        'return = λ 'x. [x];
        'fmap = λ 'f . foldr ((:) · f) [];
        'join = λ 'xns. concat xns;
    〉;
    'monad.asCategoryTheory = λ (module 'in). for (
        ('out.>>=) = λ 'a 'f. in.join (in.fmap f a);
        module 'out ∝ module in;
    ) module out;
)
