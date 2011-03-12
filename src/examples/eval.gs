module 'expr = data 'η 'α.
  | ∃ 'f | record.column f α ≥ η. var f
  | ∃ 'β. app (expr.t η (β → α)) (expr.t η β)
  | ∃ 'f 'β 'γ. lambda (α ≃ β → γ) f (expr.t (record.column f β // η) γ)
;
implicit expr.lambda refl.id;
import expr.(..\t);

lambda :: ∀ 'δ 'ε 'f 'β 'γ. (ε ≃ β → γ) → f → (expr (record.column f β // δ) γ) → expr.t δ ε;
app :: ∀ 'δ 'ε 'β. expr.t δ (β → ε) → expr.t δ β → expr.t δ ε;

λ 's 'z. s (s z) :: (α → α) → α → α

  lambda %s (lambda %z (app (var %s) (app (var %s) (var %z))))
= lambda _ refl.id %s (lambda _ refl.id %z (app (var %s) (app (var %s) (var %z))))

  %z :: %z
  --------------------------------------------------------
  〈z :: α〉 ≥ 〈z :: α〉 // 〈s :: α → α〉 // η ⊢
    var %z :: expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) α

  var %s :: expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) (α → α)
  var %z :: expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) α
  app
    :: expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) (α → α) →
      expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) α →
      expr.t (〈z :: α〉 // 〈s :: α → α〉 // η) α
  ------------------------------------------------------------------
  app (var %s) (var %z) :: expr (〈z :: α〉 // 〈s :: α → α〉 // η) α

  app (var %s) (var %z) :: expr (〈z :: α〉 // 〈s :: α → α〉 // η) α
  %z :: %z
  refl.id :: (α → α) ≃ α → α
  lambda
    :: ((α → α) ≃ α → α) → f → (expr (〈z :: α〉 // 〈s :: α → α〉 // η) α) → expr.t (〈s :: α → α〉 // η) (α → α)
  ---------------------------------------------------------------------------
  lambda _ refl.id %z (app (var %s) (app (var %s) (var %z))) :: expr (〈s :: α → α〉 // η) (α → α)

  lambda _ refl.id %z (app (var %s) (app (var %s) (var %z))) :: expr (〈s :: α → α〉// η) (α → α)
  refl.id :: (α → α) → α → α ≃ (α → α) → (α → α)
  %s :: %s
  lambda :: ((α → α) → α → α ≃ (α → α) → (α → α)) → %s → expr (〈s :: α → α〉 // η) (α → α) → expr η ((α → α) → α → α)
  --------------------------------------------------------------------------------
  lambda _ refl.id %s (lambda _ refl.id %z (app (var %s) (app (var %s) (var %z)))) :: expr η ((α → α) → α → α)
  
sig eval :: ∀ 'η 'α. η → expr.t η α → α;
'eval = λ 'env 'e. analyze e
  case var 'f. field.lookup f env
  case app 'f 'x. eval env f $ eval env x
  case lambda 'eq 'v 'e. refl.cast (refl.sym eq) (λ 'x. eval (record.insert v x env) e)
;

record.lookup :: ∀ 'f 'α 'r | record.column f α ≥ r. f → r → α;
record.insert :: ∀ 'f 'α 'r. f → α → r → record.column f α // r;

  #f = record.lookup %f
  &f = record.insert %f

refl.cast :: ∀ 'α 'β. (α ≃ β) → α → β;
refl.sym :: ∀ 'α 'β. (α ≃ β) → (β ≃ α);
