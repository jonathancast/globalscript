'foldl = λ 'f !'z 'xn. analyze xn
    case nil. z
    case 'x:'xn1. foldl f (f z x) xn1
  = λ 'f 'z 'xn. for(!⌊'z0⌋ = z; !⌊'xn0⌋ = xn;) foldl1 f z0 xn0
    where
        'foldl1 = λ 'f 'z0 'xn0. analyze xn0
            case nil. ⌊z0⌋
            case 'x:'xn1. for(!⌊'z1⌋ = f ⌊z0⌋ x; !⌊'xn2⌋ = xn1;) foldl1 f z1 xn2
;
