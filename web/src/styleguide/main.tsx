import { render } from "preact";
import "../styles/base.css";
import { Styleguide } from "./Styleguide";

const root = document.getElementById("app");
if (root) {
  render(<Styleguide />, root);
}
